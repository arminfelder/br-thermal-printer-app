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

#include "pte550w.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <cups/cups.h>
#include <memory>
#include <optional>
#include <string>
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
#include "units.h"

namespace drivers::pte550w
{
    namespace
    {
        using brother::pipeline::JobContext;
        using brother::pipeline::PageContext;

        // Spec §3 p.22: ESC i S is NOT supported by PT-E550W or PT-P750W.
        bool supportsStatusRequest(const types::ModelVariant variant)
        {
            return variant == types::ModelVariant::PtP710BT;
        }

        struct IppDeleter
        {
            void operator()(ipp_t* ipp) const noexcept { ippDelete(ipp); }
        };

        // The value of the job. If the job has none, the default of the printer.
        //
        // PAPPL 1.4.9 does not put the default into options->vendor: it keeps
        // "<name>-default" in the driver attributes, but the job code looks in the
        // printer attributes (job-process.c). A CUPS queue with a PPD cannot send the
        // option with the job, thus the default is necessary.
        std::optional<std::string> resolveOption(const pappl_pr_options_t* options, ipp_t* driverAttrs,
                                                 const char* name)
        {
            if (const char* raw = cupsGetOption(name, options->num_vendor, options->vendor))
            {
                return std::string(raw);
            }
            if (!driverAttrs)
            {
                return std::nullopt;
            }
            const std::string defaultName = std::string(name) + "-default";
            ipp_attribute_t* attr = ippFindAttribute(driverAttrs, defaultName.c_str(), IPP_TAG_ZERO);
            if (!attr)
            {
                return std::nullopt;
            }
            std::array<char, 256> value{};
            ippAttributeString(attr, value.data(), value.size());
            return std::string(value.data());
        }

        // A value that is not a number keeps `fallback`. A number out of range is clamped.
        int readClampedOption(pappl_job_t* job, const pappl_pr_options_t* options, ipp_t* driverAttrs,
                              const char* name, const int minimum, const int maximum, const int fallback)
        {
            const auto resolved = resolveOption(options, driverAttrs, name);
            if (!resolved)
            {
                return fallback;
            }
            int value = 0;
            std::size_t used = 0;
            try
            {
                value = std::stoi(*resolved, &used);
            }
            catch (const std::exception&)
            {
                used = 0;
            }
            if (used == 0 || used != resolved->size())
            {
                papplLogJob(job, PAPPL_LOGLEVEL_WARN, "%s: not a number: %s, using %d", name, resolved->c_str(),
                            fallback);
                return fallback;
            }
            const int clamped = std::clamp(value, minimum, maximum);
            if (clamped != value)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_WARN, "%s: %d is not in %d-%d, using %d", name, value, minimum,
                            maximum, clamped);
            }
            return clamped;
        }

        types::JobOptions readJobOptions(pappl_job_t* job, const pappl_pr_options_t* options)
        {
            types::JobOptions result{};
            // papplPrinterGetDriverAttributes gives a copy that the caller owns.
            const std::unique_ptr<ipp_t, IppDeleter> driverAttrs(
                papplPrinterGetDriverAttributes(papplJobGetPrinter(job)));

            if (const auto raw = resolveOption(options, driverAttrs.get(), vendorOptions::cutMode))
            {
                if (const auto mode = parseCutMode(*raw))
                {
                    result.cutMode = *mode;
                }
                else
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_WARN, "%s: unknown value %s, using %s", vendorOptions::cutMode,
                                raw->c_str(), cutModeName(result.cutMode));
                }
            }
            if (const auto raw = resolveOption(options, driverAttrs.get(), vendorOptions::chainPrinting))
            {
                result.chainPrinting = (*raw == "on");
            }
            result.marginMm = readClampedOption(job, options, driverAttrs.get(), vendorOptions::margin,
                                                vendorOptions::marginMinMm, vendorOptions::marginMaxMm,
                                                result.marginMm);
            result.cutEvery = readClampedOption(job, options, driverAttrs.get(), vendorOptions::cutEvery,
                                                vendorOptions::cutEveryMin, vendorOptions::cutEveryMax,
                                                result.cutEvery);
            return result;
        }

        // Makes the ready media of the printer equal to the TZe tape that the status block
        // reports. PAPPL gives the margins of the ready media to the clients and uses them
        // for the jobs, and the side margin is different for each tape width.
        void syncReadyMedia(pappl_printer_t* printer, const types::PrinterInfo& info)
        {
            const auto fields = info.fields();
            if (fields.mediaType != types::MediaType::LaminatedTape &&
                fields.mediaType != types::MediaType::NonLaminatedTape)
            {
                return;
            }
            const auto tape = tapeForStatusWidth(fields.mediaWidth);
            if (!tape)
            {
                return;
            }
            const auto data = std::make_unique<pappl_pr_driver_data_t>();
            if (!papplPrinterGetDriverData(printer, data.get()))
            {
                return;
            }
            auto& ready = data->media_ready[0];
            const int margin = util::units::toPwg(sideMargin(*tape));
            if (ready.size_width == tape->widthPwg && ready.left_margin == margin && ready.right_margin == margin)
            {
                return;
            }
            ready.size_width   = tape->widthPwg;
            ready.left_margin  = margin;
            ready.right_margin = margin;
            if (!pwgFormatSizeName(ready.size_name, sizeof(ready.size_name), "roll", "current", ready.size_width,
                                   ready.size_length, nullptr))
            {
                return;
            }
            if (papplPrinterSetReadyMedia(printer, data->num_source, data->media_ready))
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Tape in the printer: %s", ready.size_name);
            }
        }

        struct Traits
        {
            using Model = types::ModelVariant;
            using PrinterInfo = types::PrinterInfo;
            using RasterGraphicsTransfer = commands::RasterGraphicsTransfer;

            // Spec §2.1 p.5: 100 bytes of NULL for PT-E550W/P750W/P710BT.
            static size_t invalidateBytes(Model) { return 100; }
            // TIFF PackBits is MANDATORY: without it the printer prints blank labels.
            static constexpr bool compressRaster = true;

            static const Model* modelFor(const std::string_view mdl)
            {
                const auto it = modelVariantMap.find(mdl);
                return it != modelVariantMap.end() ? &it->second : nullptr;
            }

            static int headBytes(const Model variant) { return lineWidth.at(variant); }

            static bool acceptsMediaType(const std::string_view ippType)
            {
                return ippType == "continuous" || ippType == "labels-continuous";
            }

            // Spec §5 flow charts: invalidate, initialize, status request, control codes.
            static bool startJob(const JobContext<Model>& ctx)
            {
                return !supportsStatusRequest(ctx.model) ||
                       brother::pipeline::checkStatusAtJobStart<PrinterInfo>(ctx);
            }

            // Spec §2.1 p.5, the control codes of each page:
            //   1. ESC i a   switch dynamic command mode (raster)
            //   2. ESC i !   switch automatic status notification mode
            //   3. ESC i z   print information
            //   4. ESC i M   various mode settings
            //   5. ESC i A   specify page number in "cut each * labels"
            //   6. ESC i K   advanced mode settings
            //   7. ESC i d   specify margin amount
            //   8. M         select compression mode
            static bool startPage(const PageContext<Model>& ctx)
            {
                const auto write = [&](const std::vector<uint8_t>& data)
                {
                    return util::writeToDevice(data, ctx.device, ctx.jobId);
                };

                if (!write(commands::SwitchDynamicCommandMode{types::CommandMode::Raster}.get()) ||
                    !write(commands::SwitchAutoStatusNotify{}.get()))
                {
                    return false;
                }

                const auto jobOptions = readJobOptions(ctx.job, ctx.options);
                const auto page = pageSettings(ctx.data.model, ctx.options->header.HWResolution[1],
                                               ctx.options->header.cupsHeight, jobOptions);
                papplLogJob(ctx.job, PAPPL_LOGLEVEL_DEBUG,
                            "cut-mode=%s chain-printing=%s margin=%u dots cut-every=%u high-resolution=%d",
                            cutModeName(jobOptions.cutMode), jobOptions.chainPrinting ? "on" : "off",
                            static_cast<unsigned>(page.feedMarginDots), static_cast<unsigned>(page.cutEvery),
                            page.highResolution ? 1 : 0);
                ctx.data.lineLimit = page.rasterLines;

                // 3. Spec §4 p.32: {n3} is the tape width in mm, {n4} is 0 for continuous tape.
                const auto mediaWidth = util::units::fromPwg(ctx.options->media.size_width);
                const auto info = printInformation(tapeWidthCode(mediaWidth), page.rasterLines, ctx.pageType);
                if (!write(commands::PrintInformation{info}.get()))
                {
                    return false;
                }

                // 4. and 6. The cut mode and chain printing give ESC i M and ESC i K.
                const auto mode = buildModeBytes(jobOptions.cutMode, jobOptions.chainPrinting, ctx.data.model,
                                                 page.highResolution);
                if (!write(commands::VariousModeSettings{mode.various}.get()))
                {
                    return false;
                }

                // 5. Cut after each `cutEvery` labels.
                if (page.sendCutEvery && !write(commands::SpecifyPageNumber{page.cutEvery}.get()))
                {
                    return false;
                }

                return write(commands::AdvancedModeSettings{mode.advanced}.get()) &&
                       write(commands::SpecifyMarginAmount{page.feedMarginDots}.get()) &&
                       write(commands::SelectCompressionMode{types::CompressionMode::Compression}.get());
            }

            static unsigned statusReasons(const PrinterInfo& info) { return pte550w::statusReasons(info); }
            static bool pausesPrinting(const PrinterInfo& info) { return pte550w::pausesPrinting(info); }
            static bool resumesPrinting(const PrinterInfo& info) { return pte550w::resumesPrinting(info); }

            // Only the PT-P710BT answers ESC i S. The other models send status only during a
            // job, thus there is nothing to poll when they are idle.
            static bool pollsStatus(const Model variant) { return supportsStatusRequest(variant); }

            static void onStatus(pappl_printer_t* printer, const PrinterInfo& info) { syncReadyMedia(printer, info); }
        };

        using Pipeline = brother::pipeline::Pipeline<Traits>;
    }

    PageSettings pageSettings(const types::ModelVariant variant, const unsigned feedDpi, const unsigned cupsHeight,
                              const types::JobOptions& options)
    {
        const bool high = feedDpi == 360;
        // Millimetres to dots at 180 dpi, rounded: 2 mm is 14 dots, 127 mm is 900 dots.
        const int marginMm = std::clamp(options.marginMm, vendorOptions::marginMinMm, vendorOptions::marginMaxMm);
        const int minimum  = minFeedMargin.at(variant).numerical_value_in(util::units::dot);
        const int dots180  = std::max((marginMm * 1800 + 127) / 254, minimum);
        const auto maxLines = high ? maxRasterLines360 : maxRasterLines180;
        return PageSettings{
            .highResolution = high,
            .feedMarginDots = static_cast<uint16_t>(dots180 * (high ? 2 : 1)),
            .rasterLines    = std::min<uint32_t>(cupsHeight, maxLines),
            .sendCutEvery   = variant != types::ModelVariant::PtP710BT,
            .cutEvery       = static_cast<uint8_t>(
                std::clamp(options.cutEvery, vendorOptions::cutEveryMin, vendorOptions::cutEveryMax)),
        };
    }

    std::optional<types::CutMode> parseCutMode(const std::string_view value) noexcept
    {
        if (value == "full")         return types::CutMode::Full;
        if (value == "half")         return types::CutMode::Half;
        if (value == "none")         return types::CutMode::None;
        if (value == "special-tape") return types::CutMode::SpecialTape;
        return std::nullopt;
    }

    const char* cutModeName(const types::CutMode mode) noexcept
    {
        switch (mode)
        {
        case types::CutMode::Half:        return "half";
        case types::CutMode::None:        return "none";
        case types::CutMode::SpecialTape: return "special-tape";
        case types::CutMode::Full:
        default:                          return "full";
        }
    }

    types::ModeBytes buildModeBytes(const types::CutMode mode, const bool chainPrinting,
                                    const types::ModelVariant variant, const bool highResolution) noexcept
    {
        const auto effective = (mode == types::CutMode::Half && variant == types::ModelVariant::PtP710BT)
                                   ? types::CutMode::Full
                                   : mode;

        types::VariousModeSettingsFields various{};
        various.autoCut     = effective == types::CutMode::Full || effective == types::CutMode::Half;
        various.mirrorPrint = false;

        types::AdvancedModeSettingsFields advanced{};
        advanced.halfCut         = effective == types::CutMode::Half;
        advanced.specialTape     = effective == types::CutMode::SpecialTape;
        advanced.noChainPrinting = !chainPrinting;
        advanced.highResolution  = highResolution;

        types::ModeBytes bytes{};
        bytes.various.set_fields(various);
        bytes.advanced.set_fields(advanced);
        return bytes;
    }

    std::optional<Tape> tapeForStatusWidth(const uint8_t statusWidth) noexcept
    {
        const auto it = std::ranges::find(tapes, statusWidth, &Tape::statusWidth);
        return it != tapes.end() ? std::optional<Tape>(*it) : std::nullopt;
    }

    std::optional<Tape> tapeForMediaWidth(const util::units::MediaSize width) noexcept
    {
        // The nominal width and the real width are different by up to 0.1 mm (3.5 mm is
        // 3.4 mm, 6 mm is 5.9 mm). The widths are 2.5 mm or more apart.
        constexpr int tolerancePwg = 60;
        const int pwg = util::units::toPwg(width);
        const auto it = std::ranges::find_if(
            tapes, [pwg](const Tape& tape) { return std::abs(tape.widthPwg - pwg) <= tolerancePwg; });
        return it != tapes.end() ? std::optional<Tape>(*it) : std::nullopt;
    }

    util::units::MediaSize sideMargin(const Tape& tape) noexcept
    {
        // Dots at 180 dpi to hundredths of a millimetre, rounded up.
        return util::units::fromPwg((tape.sideMarginDots * 2540 + 179) / 180);
    }

    uint8_t tapeWidthCode(const util::units::MediaSize width) noexcept
    {
        if (const auto tape = tapeForMediaWidth(width))
        {
            return tape->statusWidth;
        }
        return static_cast<uint8_t>(std::clamp((util::units::toPwg(width) + 50) / 100, 0, 255));
    }

    types::PrintInfoFields printInformation(const unsigned mediaWidthMm, const uint32_t rasterLines,
                                            const types::PageType pageType)
    {
        types::PrintInfoFields info{};
        info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_recover) |
                           static_cast<uint8_t>(types::PrintInfoFlags::pi_width);
        info.mediaType   = 0x00;
        info.mediaWidth  = static_cast<uint8_t>(mediaWidthMm);
        info.mediaLength = 0;
        std::ranges::copy(std::bit_cast<std::array<uint8_t, 4>>(rasterLines), info.rasterNumber);
        info.pageType = pageType;
        info.quality  = 0x00;
        return info;
    }

    unsigned statusReasons(const types::PrinterInfo& info)
    {
        const auto fields = info.fields();
        unsigned status = PAPPL_PREASON_NONE;
        if (fields.errorInformation2.coverOpen)
            status |= PAPPL_PREASON_COVER_OPEN;
        if (fields.errorInformation1.noMedia)
            status |= PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED;
        if (fields.errorInformation2.wrongMedia)
            status |= PAPPL_PREASON_MEDIA_NEEDED;
        if (fields.errorInformation1.cutterJam)
            status |= PAPPL_PREASON_MEDIA_JAM;
        if (fields.errorInformation1.weakBatteries || fields.errorInformation2.overheating)
            status |= PAPPL_PREASON_OTHER;

        if (fields.statusType == types::StatusType::TurnedOff)
            status |= PAPPL_PREASON_OFFLINE;
        if (fields.statusType == types::StatusType::Notification &&
            fields.notificationNumber == types::NotificationNumber::CoverOpen)
            status |= PAPPL_PREASON_COVER_OPEN;
        return status;
    }

    bool pausesPrinting(const types::PrinterInfo&) { return false; }

    bool resumesPrinting(const types::PrinterInfo&) { return false; }

    bool updateDriverData(pappl_pr_driver_data_t *driverData,
                          const std::string_view &model, ipp_t** driverAttrs)
    {
        // An unknown model must give a clean IPP failure, not an exception that
        // terminates the server.
        const auto variant = Traits::modelFor(model);
        if (!driverData || !variant)
        {
            return false;
        }

        static constexpr std::array<const char*, 2> sources{"main-roll", "auto"};
        // The default media is the 24 mm tape. syncReadyMedia changes the margins when
        // the printer reports a different tape.
        const auto side = sideMargin(tapes.back());
        const brother::Margins defaultMargins{.left = side, .right = side};
        const brother::DriverDataSpec spec{
            .model      = model,
            .kind       = PAPPL_KIND_LABEL,
            .resolutions = resolutions,
            .margins    = defaultMargins,
            .media      = defaultMedia,
            .mediaTypes = mediaTypes,
            .sources    = sources,
        };
        if (!brother::fillDriverData(driverData, spec))
        {
            return false;
        }

        // PAPPL prints a dot when the gray value is less than or equal to the dither value.
        // The default blue-noise matrix has a 255 cell, thus white areas get a dot grid.
        for (auto *matrix : {&driverData->gdither, &driverData->pdither})
            for (auto &row : *matrix)
                for (auto &cell : row)
                    cell = std::min<unsigned char>(cell, 254);

        driverData->num_vendor = 4;
        driverData->vendor[0]  = vendorOptions::cutMode;
        driverData->vendor[1]  = vendorOptions::chainPrinting;
        driverData->vendor[2]  = vendorOptions::margin;
        driverData->vendor[3]  = vendorOptions::cutEvery;

        // PAPPL gives a null *driverAttrs. PAPPL copies the attributes and deletes this set.
        if (driverAttrs)
        {
            if (!*driverAttrs)
            {
                *driverAttrs = ippNew();
            }
            ippAddStrings(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "brother-cut-mode-supported",
                          static_cast<int>(vendorOptions::cutModeValues.size()), nullptr,
                          vendorOptions::cutModeValues.data());
            ippAddString(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "brother-cut-mode-default", nullptr,
                         vendorOptions::cutModeValues.front());
            ippAddStrings(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "brother-chain-printing-supported",
                          static_cast<int>(vendorOptions::chainPrintingValues.size()), nullptr,
                          vendorOptions::chainPrintingValues.data());
            ippAddString(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "brother-chain-printing-default", nullptr,
                         vendorOptions::chainPrintingValues.front());
            ippAddRange(*driverAttrs, IPP_TAG_PRINTER, "brother-margin-supported", vendorOptions::marginMinMm,
                        vendorOptions::marginMaxMm);
            ippAddInteger(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "brother-margin-default",
                          vendorOptions::marginMinMm);
            ippAddRange(*driverAttrs, IPP_TAG_PRINTER, "brother-cut-every-supported", vendorOptions::cutEveryMin,
                        vendorOptions::cutEveryMax);
            ippAddInteger(*driverAttrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "brother-cut-every-default",
                          vendorOptions::cutEveryMin);
        }

        Pipeline::install(driverData, *variant);
        return true;
    }
}
