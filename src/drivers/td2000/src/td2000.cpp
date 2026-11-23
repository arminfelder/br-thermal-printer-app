//    Copyright (C) 2026  Armin Felder
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "td2000.h"
#include "units.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <mp-units/systems/si/unit_symbols.h>
#include <optional>
#include <pappl/log.h>
#include <pappl/printer.h>
#include <string_view>
#include <vector>

extern "C" {
#include <pappl/base.h>
}

#include "driver_data.h"
#include "raster_pipeline.h"
#include "util.h"

namespace drivers::td2000
{
    namespace
    {
        std::optional<types::MediaType> mediaTypeFor(const std::string_view ippType)
        {
            if (ippType == "labels-continuous" || ippType == "continuous")
            {
                return types::MediaType::ContinuousLengthTape;
            }
            if (ippType == "labels")
            {
                return types::MediaType::DieCutLabels;
            }
            return std::nullopt;
        }

        bool send(const brother::pipeline::PageContext<types::ModelFamily>& ctx,
                  const std::vector<uint8_t>& data, const char* what)
        {
            if (!util::writeToDevice(data, ctx.device, ctx.jobId))
            {
                papplLogJob(ctx.job, PAPPL_LOGLEVEL_ERROR, "failed to send %s", what);
                return false;
            }
            return true;
        }

        struct Traits
        {
            using Model = types::ModelFamily;
            using PrinterInfo = types::PrinterInfo;
            using RasterGraphicsTransfer = commands::RasterGraphicsTransfer;

            // Raster spec 2.1: 200 bytes of NULL for every TD-2000 model.
            static size_t invalidateBytes(Model) { return 200; }
            static constexpr bool compressRaster = false;

            static const Model* modelFor(const std::string_view mdl)
            {
                const auto it = modelFamilyMap.find(mdl);
                return it != modelFamilyMap.end() ? &it->second : nullptr;
            }

            static int headBytes(const Model family) { return lineWidth.at(family); }

            static bool acceptsMediaType(const std::string_view ippType) { return mediaTypeFor(ippType).has_value(); }

            // Raster spec flow chart 5.1: status request, then the control codes. ESC i a 01h
            // selects raster mode. It must come before all raster data.
            static bool startJob(const brother::pipeline::JobContext<Model>& ctx)
            {
                return brother::pipeline::checkStatusAtJobStart<PrinterInfo>(ctx) &&
                       util::writeToDevice(commands::SwitchDynamicCommandMode{types::CommandMode::Raster}.get(),
                                           ctx.device, ctx.jobId);
            }

            // Raster spec 2.1, the control codes of each page:
            //   1. ESC i U  additional media information
            //   2. ESC i z  print information
            //   3. ESC i M  various mode settings
            //   4. ESC i d  specify margin amount
            //   5. M        select compression mode
            static bool startPage(const brother::pipeline::PageContext<Model>& ctx)
            {
                const auto* options = ctx.options;
                const auto requestedType = mediaTypeFor(options->media.type);
                if (!requestedType)
                {
                    return false;
                }
                const auto mediaType = *requestedType;

                // Warn one time for each page, not for each line, when the media is
                // narrower than the print head. The line gets zero padding, thus the
                // operator sees blank tape where an image can be expected.
                if (const auto deliveredBytes = static_cast<size_t>(options->header.cupsBytesPerLine);
                    deliveredBytes < static_cast<size_t>(ctx.data.bytesPerLine))
                {
                    papplLogJob(ctx.job, PAPPL_LOGLEVEL_WARN,
                                "Raster line is %zu bytes but the print head is %d; "
                                "media is narrower than the head, line will be padded",
                                deliveredBytes, ctx.data.bytesPerLine);
                }

                // 1. ESC i U updates the media record of the printer.
                const auto mediaWidth  = util::units::fromPwg(options->media.size_width);
                const auto mediaLength = util::units::fromPwg(options->media.size_length);
                const auto mediaInfo = media::getMediaInfoForMedia(ctx.data.model, mediaWidth, mediaLength, mediaType);
                if (mediaInfo == media::none)
                {
                    papplLogJob(ctx.job, PAPPL_LOGLEVEL_ERROR, "Unsupported media dimensions for model family");
                    return false;
                }
                if (!send(ctx, commands::AdditionalMediaInformation{mediaInfo}.get(), "additionalMediaInfo"))
                {
                    return false;
                }

                // 2. ESC i z. {n3}/{n4} must agree with the ESC i U record. {n5..n8} is the
                // raster line count, little-endian. {n10} is fixed at 0.
                types::PrintInfoFields info{};
                info.mediaType = static_cast<uint8_t>(mediaType);
                info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) |
                                   static_cast<uint8_t>(types::PrintInfoFlags::pi_width) |
                                   static_cast<uint8_t>(types::PrintInfoFlags::pi_recover) |
                                   static_cast<uint8_t>(types::PrintInfoFlags::pi_length);
                info.pageType = ctx.pageType;
                const auto printMedia = media::printInfoMedia(mediaInfo, mediaType, mediaLength);
                info.mediaWidth  = printMedia.width;
                info.mediaLength = printMedia.length;
                if (!printMedia.lengthValid)
                {
                    info.validFields &= static_cast<uint8_t>(~static_cast<uint8_t>(types::PrintInfoFlags::pi_length));
                }
                const auto rasterBytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(options->header.cupsHeight));
                std::ranges::copy(rasterBytes, info.rasterNumber);
                if (!send(ctx, commands::PrintInformation{info}.get(), "printInfo"))
                {
                    return false;
                }

                // 3. ESC i M: no rotation, no peeler.
                constexpr types::VariousModeSettings settings{};
                if (!send(ctx, commands::VariousModeSettings{settings}.get(), "variousModeSettings"))
                {
                    return false;
                }

                // 4. ESC i d: die-cut labels must use 0 ("only 0 is available").
                const util::units::DotCount feedMargin =
                    (mediaType == types::MediaType::ContinuousLengthTape)
                        ? minFeedMargin.at(ctx.data.model)
                        : 0 * util::units::dot;
                if (!send(ctx,
                          commands::SpecifyMarginAmount{
                              static_cast<uint16_t>(feedMargin.numerical_value_in(util::units::dot))}.get(),
                          "specifyMargin"))
                {
                    return false;
                }

                // 5. M 00h: no compression.
                return send(ctx, commands::SelectCompressionMode{types::CompressionMode::NoCompression}.get(),
                            "compressionModeCommand");
            }

            static unsigned statusReasons(const PrinterInfo& info) { return td2000::statusReasons(info); }
            static bool pausesPrinting(const PrinterInfo& info) { return td2000::pausesPrinting(info); }
            static bool resumesPrinting(const PrinterInfo& info) { return td2000::resumesPrinting(info); }

            // All TD-2000 models answer ESC i S.
            static bool pollsStatus(Model) { return true; }
        };

        using Pipeline = brother::pipeline::Pipeline<Traits>;
    }

    namespace
    {
        bool isNotification(const types::PrinterInfo& info, const types::NotificationNumber a,
                            const types::NotificationNumber b)
        {
            const auto fields = info.fields();
            return fields.statusType == types::StatusType::Notification &&
                   (fields.notificationNumber == a || fields.notificationNumber == b);
        }
    }

    unsigned statusReasons(const types::PrinterInfo& info)
    {
        using types::NotificationNumber;
        const auto fields = info.fields();
        unsigned status = PAPPL_PREASON_NONE;
        if (fields.errorInformation2.coverOpen)
            status |= PAPPL_PREASON_COVER_OPEN;
        if (fields.errorInformation1.noMedia || fields.errorInformation1.endOfMedia)
            status |= PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED;
        if (fields.errorInformation2.replacingMedia)
            status |= PAPPL_PREASON_MEDIA_NEEDED;
        if (fields.errorInformation2.mediaCannotBeFed)
            status |= PAPPL_PREASON_MEDIA_JAM | PAPPL_PREASON_MEDIA_EMPTY;
        if (fields.errorInformation2.systemError || fields.errorInformation2.communicationError)
            status |= PAPPL_PREASON_OTHER;
        // "Printer in use" can be set while this job prints. Report it only in an error block.
        if (fields.errorInformation1.printerInUse && fields.statusType == types::StatusType::ErrorOccurred)
            status |= PAPPL_PREASON_OTHER;

        if (fields.statusType == types::StatusType::TurnedOff)
            status |= PAPPL_PREASON_OFFLINE;
        if (pausesPrinting(info) ||
            isNotification(info, NotificationNumber::WaitingForPeeling, NotificationNumber::WaitingForPeeling))
            status |= PAPPL_PREASON_OTHER;
        return status;
    }

    bool pausesPrinting(const types::PrinterInfo& info)
    {
        return isNotification(info, types::NotificationNumber::CoolingStarted,
                              types::NotificationNumber::PrinterPaused);
    }

    bool resumesPrinting(const types::PrinterInfo& info)
    {
        return isNotification(info, types::NotificationNumber::CoolingFinished,
                              types::NotificationNumber::FinishedPrinterPaused);
    }

    namespace media
    {
        types::MediaInfo getMediaInfoForMedia(const types::ModelFamily family, const util::units::MediaSize width, const util::units::MediaSize length, const types::MediaType type) noexcept
        {
            using mp_units::si::unit_symbols::mm;

            //TODO: use media information from the printer instead of static data
            auto pick = [&](const types::MediaInfo& td2x2x_val, const types::MediaInfo& td2x3x_val) -> types::MediaInfo {
                return (family == types::ModelFamily::Td2x3x) ? td2x3x_val : td2x2x_val;
            };

            switch (type)
            {
            case types::MediaType::ContinuousLengthTape:
                return (width <= 57 * mm) ? pick(td2x2x::_57mm, td2x3x::_57mm)
                                          : pick(td2x2x::_58mm, td2x3x::_58mm);

            case types::MediaType::DieCutLabels:
            {
                // Sorted by width, then length. Select the first entry that is not smaller than
                // the request in both dimensions. This is the smallest label that holds the job.
                struct DieCutEntry
                {
                    util::units::MediaSize width;
                    util::units::MediaSize length;
                    const types::MediaInfo& td2x2x_val;
                    const types::MediaInfo& td2x3x_val;
                };
                static const std::array dieCutTable{
                    DieCutEntry{30 * mm, 30 * mm, td2x2x::_30x30mm, td2x3x::_30x30mm},
                    DieCutEntry{40 * mm, 40 * mm, td2x2x::_40x40mm, td2x3x::_40x40mm},
                    DieCutEntry{40 * mm, 50 * mm, td2x2x::_40x50mm, td2x3x::_40x50mm},
                    DieCutEntry{40 * mm, 60 * mm, td2x2x::_40x60mm, td2x3x::_40x60mm},
                    DieCutEntry{50 * mm, 30 * mm, td2x2x::_50x30mm, td2x3x::_50x30mm},
                    DieCutEntry{51 * mm, 26 * mm, td2x2x::_51x26mm, td2x3x::_51x26mm},
                    DieCutEntry{60 * mm, 60 * mm, td2x2x::_60x60mm, td2x3x::_60x60mm},
                };
                for (const auto& entry : dieCutTable)
                {
                    if (width <= entry.width && length <= entry.length)
                    {
                        return pick(entry.td2x2x_val, entry.td2x3x_val);
                    }
                }
                return none; // dimensions outside handled ranges
            }

            case types::MediaType::NoMedia:
            default:
                return none;
            }
        }

        PrintInfoMedia printInfoMedia(const types::MediaInfo& info,
                                      const types::MediaType type,
                                      [[maybe_unused]] const util::units::MediaSize requestedLength) noexcept
        {
            const auto record = info.fields();

            if (type == types::MediaType::DieCutLabels)
            {
                // The label has a fixed length, thus the record holds both dimensions.
                return {record.paperWidth, record.paperLengthMm, true};
            }

            // Continuous tape: raster spec 4 table (3) (a) fixes the media length at 00h.
            return {record.paperWidth, 0, false};
        }
    }

    bool updateDriverData(pappl_pr_driver_data_t* driverData, const std::string_view& model,
                          [[maybe_unused]] ipp_t** driverAttrs)
    {
        // An unknown model must give a clean IPP failure, not an exception that
        // terminates the server.
        const auto family = Traits::modelFor(model);
        if (!driverData || !family)
        {
            return false;
        }

        static constexpr std::array<const char*, 1> sources{"main-roll"};
        const brother::DriverDataSpec spec{
            .model      = model,
            .kind       = static_cast<pappl_kind_t>(PAPPL_KIND_RECEIPT | PAPPL_KIND_LABEL),
            .resolutions = std::span(&resolutions.at(*family), 1),
            .margins    = margins.at(*family),
            .media      = defaultMedia,
            .mediaTypes = mediaTypes,
            .sources    = sources,
        };
        if (!brother::fillDriverData(driverData, spec))
        {
            return false;
        }
        Pipeline::install(driverData, *family);
        return true;
    }
}
