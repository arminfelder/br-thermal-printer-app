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
#include <memory>
#include <optional>
#include <span>
#include <thread>

extern "C" {
#include <pappl/pappl.h>
#include <pappl/base.h>
}

#include "util.h"

namespace drivers::pte550w
{
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    // Attempts one non-blocking read and returns the 32-byte status packet if
    // byte 0 (printHeadMark) equals 0x80 (spec §4 p.22, Table 4).
    static std::optional<types::PrinterInfo> readStatusPacket(pappl_device_t *device)
    {
        types::PrinterInfo info{};
        const auto result = papplDeviceRead(device,
                                             info.raw.data(),
                                             info.raw.size());
        if (result == static_cast<ssize_t>(info.raw.size()) &&
            info.fields().printHeadMark == 0x80)
        {
            return info;
        }
        return std::nullopt;
    }

    namespace callbacks
    {
        // ------------------------------------------------------------------
        bool print([[maybe_unused]] pappl_job_t     *job,
                   [[maybe_unused]] pappl_pr_options_t *options,
                   [[maybe_unused]] pappl_device_t   *device)
        {
            return (job && options && device);
        }

        // ------------------------------------------------------------------
        bool startJob(pappl_job_t *job, pappl_pr_options_t *options,
                      pappl_device_t *device)
        {
            if (!job || !options || !device)
                return false;

            const auto jobId = papplJobGetID(job);

            // Resolve model variant from the IEEE 1284 device-ID string.
            std::array<char, 128> deviceId{};
            if (papplDeviceGetID(device, deviceId.data(), deviceId.size()) == nullptr)
                return false;

            cups_option_t *did     = nullptr;
            const int     numOpts  = papplDeviceParseID(deviceId.data(), &did);
            const char   *mdl      = cupsGetOption("MDL", numOpts, did);
            const std::string model = mdl ? mdl : "";
            cupsFreeOptions(numOpts, did);

            if (model.empty())
                return false;

            const auto variantIt = modelVariantMap.find(model);
            if (variantIt == modelVariantMap.end())
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                            "Unknown PT-E550W/P750W/P710BT model: %s", model.c_str());
                return false;
            }
            const auto variant = variantIt->second;

            auto jobData = std::make_unique<types::JobData>(types::JobData{
                .variant        = variant,
                .useCompression = true,   // MANDATORY for this printer family
                .bytesPerLine   = lineWidth.at(variant),
            });

            // Spec §5 flow charts: Invalidate → Initialize → Status request → control codes.
            // Invalidate: 100 NULL bytes (spec §2.1 p.5).
            if (!util::writeToDevice(commands::Invalidate{}.get(), device, jobId))
                return false;

            // ESC @ — Initialize.
            if (!util::writeToDevice(commands::Initialize{}.get(), device, jobId))
                return false;

            // Spec §3 p.22: ESC i S is NOT supported by PT-E550W or PT-P750W.
            // Only send it for PT-P710BT.
            if (variant == types::ModelVariant::PtP710BT)
            {
                if (!util::writeToDevice(
                        commands::StatusInformationRequest{}.get(),
                        device, jobId))
                    return false;

                if (const auto packet = readStatusPacket(device))
                {
                    if (packet->fields().statusType == types::StatusType::ErrorOccurred)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                    "Printer reported error 0x%02x 0x%02x",
                                    std::bit_cast<uint8_t>(packet->fields().errorInformation1),
                                    std::bit_cast<uint8_t>(packet->fields().errorInformation2));
                        return false;
                    }
                }
            }

            // Mark push-status mode so getStatus() does not issue ESC i S
            // while data transmission is ongoing (spec §4, ESC i S note).
            const auto printer = papplJobGetPrinter(job);
            const auto uri     = papplPrinterGetDeviceURI(printer);
            {
                std::lock_guard lock(printerStateMutex);
                printerStatusPolling[uri] = true;
            }

            papplJobSetData(job, jobData.release());
            return true;
        }

        // ------------------------------------------------------------------
        // Spec §2.1 p.5 control code sequence (per page):
        //   1. ESC i a   — Switch dynamic command mode (raster)
        //   2. ESC i !   — Switch automatic status notification mode
        //   3. ESC i z   — Print information command
        //   4. ESC i M   — Various mode settings
        //   5. ESC i A   — Specify page number in "cut each * labels"
        //   6. ESC i K   — Advanced mode settings
        //   7. ESC i d   — Specify margin amount
        //   8. M         — Select compression mode
        bool startPage(pappl_job_t *job, pappl_pr_options_t *options,
                       pappl_device_t *device, unsigned pageNumber)
        {
            if (!job || !options || !device)
                return false;

            const auto jobId = papplJobGetID(job);

            const std::string_view mediaTypeStr(options->media.type);
            if (mediaTypeStr != "continuous" && mediaTypeStr != "labels-continuous")
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                            "Unsupported media type for PT-E550W/P750W/P710BT: %s",
                            options->media.type);
                return false;
            }

            const auto jobData = static_cast<types::JobData *>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }

            // 1. ESC i a — Switch to raster mode.
            if (!util::writeToDevice(
                    commands::SwitchDynamicCommandMode{types::CommandMode::Raster}.get(),
                    device, jobId))
                return false;

            // 2. ESC i ! — Switch automatic status notification (notify=0).
            if (!util::writeToDevice(
                    commands::SwitchAutoStatusNotify{}.get(), device, jobId))
                return false;

            // 3. ESC i z — Print information.
            //    Spec §4 p.32: {n3}=media width (mm), {n4}=media length (0 for continuous).
            //    validFlags: pi_recover|pi_width = 0x84 (spec sample §2.2.3 p.11).
            types::PrintInfoFields info{};
            info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_recover)
                             | static_cast<uint8_t>(types::PrintInfoFlags::pi_width)
                             | static_cast<uint8_t>(types::PrintInfoFlags::pi_kind);
            info.mediaType   = static_cast<uint8_t>(types::MediaType::LaminatedTape);
            info.mediaWidth  = static_cast<uint8_t>(options->media.size_width / 100);
            info.mediaLength = 0;

            info.pageType    = (pageNumber == 0) ? types::PageType::startingPage
                                                 : types::PageType::otherPage;

            const auto rasterLineCount =
                static_cast<uint32_t>(options->header.cupsHeight);
            const auto rasterBytes =
                std::bit_cast<std::array<uint8_t, 4>>(rasterLineCount);
            std::ranges::copy(rasterBytes, info.rasterNumber);

            info.quality = 0x00;

            if (!util::writeToDevice(
                    commands::PrintInformation{info}.get(), device, jobId))
                return false;

            // 4. ESC i M — Various mode settings.
            //    Spec §4 p.33: bit 6=auto cut, bit 7=mirror. Enable auto cut.
            types::VariousModeSettingsFields vmsFields{};
            vmsFields.autoCut     = true;
            vmsFields.mirrorPrint = false;
            types::VariousModeSettings settings{};
            settings.set_fields(vmsFields);

            if (!util::writeToDevice(
                    commands::VariousModeSettings{settings}.get(), device, jobId))
                return false;

            // 5. ESC i A — Specify the page number in "cut each * labels".
            //    Default: 1 (cut each label).
            if (!util::writeToDevice(
                    commands::SpecifyPageNumber{1}.get(), device, jobId))
                return false;

            // 6. ESC i K — Advanced mode settings.
            //    Spec §4 p.33: bit 3=no chain printing (1=feed+cut after last).
            types::AdvancedModeSettingsFields amsFields{};
            amsFields.noChainPrinting = true;
            types::AdvancedModeSettings ams{};
            ams.set_fields(amsFields);

            if (!util::writeToDevice(
                    commands::AdvancedModeSettings{ams}.get(), device, jobId))
                return false;

            // 7. ESC i d — Specify margin amount.
            //    Minimum 14 dots at 180 dpi, 28 dots at 360 dpi (spec §2.3.3).
            uint8_t feedMargin = minFeedMarginDots.at(jobData->variant);
            if (options->header.HWResolution[0] == 360)
                feedMargin *= 2;

            if (!util::writeToDevice(
                    commands::SpecifyMarginAmount{feedMargin, 0}.get(), device, jobId))
                return false;

            // 8. M — Select compression mode.
            //    TIFF PackBits is mandatory for this printer family.
            if (!util::writeToDevice(
                    commands::SelectCompressionMode{
                        types::CompressionMode::Compression}.get(),
                    device, jobId))
                return false;

            return true;
        }

        // ------------------------------------------------------------------
        void applyPrinterStatus(pappl_printer_t *printer,
                                const types::PrinterInfo &info,
                                const char *uri)
        {
            {
                std::lock_guard lock(printerStateMutex);
                printerStatus[uri] = info;
            }

            const auto fields = info.fields();
            if (fields.statusType == types::StatusType::ErrorOccurred)
            {
                unsigned int status = PAPPL_PREASON_NONE;
                if (fields.errorInformation2.coverOpen)
                    status |= PAPPL_PREASON_COVER_OPEN;
                if (fields.errorInformation1.noMedia)
                    status |= PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED;
                if (fields.errorInformation2.wrongMedia)
                    status |= PAPPL_PREASON_MEDIA_NEEDED;
                if (fields.errorInformation1.cutterJam)
                    status |= PAPPL_PREASON_MEDIA_JAM;
                if (fields.errorInformation1.weakBatteries ||
                    fields.errorInformation2.overheating)
                    status |= PAPPL_PREASON_OTHER;
                papplPrinterSetReasons(printer,
                                       static_cast<pappl_preason_t>(status),
                                       PAPPL_PREASON_DEVICE_STATUS);
            }
            else if (fields.statusType == types::StatusType::TurnedOff)
            {
                papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE,
                                       PAPPL_PREASON_DEVICE_STATUS);
            }
            else if (fields.statusType == types::StatusType::Notification)
            {
                if (fields.notificationNumber ==
                        types::NotificationNumber::CoolingStarted   ||
                    fields.notificationNumber ==
                        types::NotificationNumber::WaitingForPeeling ||
                    fields.notificationNumber ==
                        types::NotificationNumber::PrinterPaused)
                {
                    papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER,
                                           PAPPL_PREASON_DEVICE_STATUS);
                }
                else
                {
                    papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                           PAPPL_PREASON_DEVICE_STATUS);
                }
            }
            else
            {
                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE,
                                       PAPPL_PREASON_DEVICE_STATUS);
            }
        }

        // ------------------------------------------------------------------
        bool waitForPrinterResume(pappl_job_t *job, pappl_device_t *device,
                                   pappl_printer_t *printer, const char *uri)
        {
            constexpr int kMaxWaitMs = 120'000;
            constexpr int kDelayMs   = 100;

            for (int waited = 0; waited < kMaxWaitMs; waited += kDelayMs)
            {
                if (const auto packet = readStatusPacket(device))
                {
                    const auto fields = packet->fields();
                    applyPrinterStatus(printer, *packet, uri);
                    if (fields.statusType == types::StatusType::Notification &&
                        (fields.notificationNumber ==
                             types::NotificationNumber::CoolingFinished ||
                         fields.notificationNumber ==
                             types::NotificationNumber::FinishedPrinterPaused))
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Printer resumed");
                        return true;
                    }
                    if (fields.statusType == types::StatusType::ErrorOccurred)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                    "Printer error while waiting to resume");
                        return false;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs));
            }
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                        "Timed out waiting for printer to resume");
            return false;
        }

        // ------------------------------------------------------------------
        bool writeLine(pappl_job_t *job, pappl_pr_options_t *options,
                       pappl_device_t *device,
                       [[maybe_unused]] unsigned pageNumber,
                       const unsigned char *pixels)
        {
            if (!pixels || !options || !job || !device)
                return false;

            const auto jobData =
                static_cast<types::JobData *>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }

            // Printer head: 128 pins = 16 bytes. PAPPL may deliver more bytes
            // (e.g. 22 for 24mm at 180dpi), but the first 16 bytes cover the
            // 128 print-area pins. The extra bytes are right-side zeros.
            constexpr unsigned kPrinterBytes = 16;

#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            const std::span lineData(pixels, kPrinterBytes);
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif
            const auto mirroredLine = util::mirrorLine(lineData);
            const auto compressed = util::compressLine(mirroredLine);
            const commands::RasterGraphicsTransfer rgt{
                static_cast<int>(compressed.size()), compressed};
            const auto rasterCmd = rgt.get();

            const auto printer = papplJobGetPrinter(job);
            const auto uri     = papplPrinterGetDeviceURI(printer);

            for (int retry = 0; retry <= 1; ++retry)
            {
                const auto written =
                    papplDeviceWrite(device, rasterCmd.data(), rasterCmd.size());
                if (written == static_cast<ssize_t>(rasterCmd.size()))
                {
                    papplDeviceFlush(device);

                    if (const auto packet = readStatusPacket(device))
                    {
                        const auto fields = packet->fields();
                        applyPrinterStatus(printer, *packet, uri);

                        if (fields.statusType == types::StatusType::ErrorOccurred)
                            return false;
                        if (fields.statusType == types::StatusType::Notification &&
                            (fields.notificationNumber ==
                                 types::NotificationNumber::CoolingStarted ||
                             fields.notificationNumber ==
                                 types::NotificationNumber::PrinterPaused))
                        {
                            if (!waitForPrinterResume(job, device, printer, uri))
                                return false;
                        }
                    }
                    return true;
                }

                if (retry >= 1) break;

                papplLogJob(job, PAPPL_LOGLEVEL_WARN,
                            "Raster write failed, checking for cooling or pause");

                std::optional<types::PrinterInfo> statusPacket;
                for (int attempt = 0; attempt < 20; ++attempt)
                {
                    statusPacket = readStatusPacket(device);
                    if (statusPacket) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (!statusPacket) return false;

                const auto fields = statusPacket->fields();
                applyPrinterStatus(printer, *statusPacket, uri);

                if (fields.statusType == types::StatusType::Notification &&
                    (fields.notificationNumber ==
                         types::NotificationNumber::CoolingStarted ||
                     fields.notificationNumber ==
                         types::NotificationNumber::PrinterPaused))
                {
                    if (!waitForPrinterResume(job, device, printer, uri))
                        return false;
                }
                else
                    return false;
            }
            return false;
        }

        // ------------------------------------------------------------------
        bool endPage(pappl_job_t *job, pappl_pr_options_t *options,
                     pappl_device_t *device, const unsigned pageNumber)
        {
            if (!job || !options || !device)
                return false;

            const auto pageCount =
                static_cast<unsigned>(papplJobGetImpressions(job));
            if (pageNumber < pageCount - 1)
            {
                const auto jobId = papplJobGetID(job);
                if (!util::writeToDevice(commands::Print{}.get(), device, jobId))
                    return false;
            }
            return true;
        }

        // ------------------------------------------------------------------
        bool endJob(pappl_job_t *job, pappl_pr_options_t *options,
                    pappl_device_t *device)
        {
            if (!job || !options || !device)
                return false;

            const auto jobId = papplJobGetID(job);

            // Spec §4: Control-Z (1Ah) — Print with feeding; last page.
            if (!util::writeToDevice(
                    commands::PrintWithFeeding{}.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                            "Failed to send PrintWithFeeding");
                return false;
            }

            // Drain status packets until the printer returns to receiving state
            // (spec §5.1 normal flow: PhaseChange Printing → PrintingCompleted →
            //  PhaseChange Receiving).
            constexpr int kMaxAttempts = 200;
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                if (const auto packet = readStatusPacket(device))
                {
                    const auto fields = packet->fields();
                    if (fields.statusType == types::StatusType::PhaseChange &&
                        fields.phaseType  == types::Phase::ReceivingState)
                        break;
                    if (fields.statusType == types::StatusType::ErrorOccurred)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                    "Printer error during job completion");
                        break;
                    }
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // Return to pull-mode so getStatus() can issue ESC i S again.
            const auto printer = papplJobGetPrinter(job);
            const auto uri     = papplPrinterGetDeviceURI(printer);
            {
                std::lock_guard lock(printerStateMutex);
                printerStatusPolling[uri] = false;
            }

            const auto jobData = static_cast<types::JobData *>(papplJobGetData(job));
            papplJobSetData(job, nullptr);
            delete jobData;

            return true;
        }

        // ------------------------------------------------------------------
        bool getStatus(pappl_printer_t *printer)
        {
            const auto uri = papplPrinterGetDeviceURI(printer);

            bool inPushMode = false;
            {
                std::lock_guard lock(printerStateMutex);
                printerStatusPolling.try_emplace(uri, false);
                inPushMode = printerStatusPolling.at(uri);
            }

            if (inPushMode)
            {
                // Do not send ESC i S while printing; just read any pushed packet.
                const auto device = papplPrinterOpenDevice(printer);
                if (!device)
                    return true;

                if (const auto packet = readStatusPacket(device))
                    applyPrinterStatus(printer, *packet, uri);

                papplPrinterCloseDevice(printer);
                return true;
            }

            // Pull mode: only PT-P710BT supports ESC i S (spec §3 p.22).
            // For PT-E550W and PT-P750W the printer pushes status automatically
            // during printing; in idle state we have no pull mechanism, so we
            // just report OK (the last pushed status is already applied).
            std::array<char, 128> deviceId{};
            {
                const auto device = papplPrinterOpenDevice(printer);
                if (device)
                {
                    papplDeviceGetID(device, deviceId.data(), deviceId.size());
                    papplPrinterCloseDevice(printer);
                }
            }
            cups_option_t *did = nullptr;
            const int numOpts = papplDeviceParseID(deviceId.data(), &did);
            const char *mdl   = cupsGetOption("MDL", numOpts, did);
            const std::string model = mdl ? mdl : "";
            cupsFreeOptions(numOpts, did);

            const auto variantIt = modelVariantMap.find(model);
            if (variantIt == modelVariantMap.end() ||
                variantIt->second != types::ModelVariant::PtP710BT)
            {
                // Not P710BT — no ESC i S support, nothing to poll.
                return true;
            }

            // PT-P710BT: send ESC i S and read the 32-byte response.
            const auto device = papplPrinterOpenDevice(printer);
            if (!device)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN,
                                "getStatus: could not open device");
                return false;
            }

            if (!util::writeToDevice(
                    commands::StatusInformationRequest{}.get(), device, -1))
            {
                papplPrinterCloseDevice(printer);
                return false;
            }

            std::optional<types::PrinterInfo> received;
            constexpr int kMaxAttempts = 100;
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                received = readStatusPacket(device);
                if (received) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            papplPrinterCloseDevice(printer);

            if (!received)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN,
                                "getStatus: timed out waiting for status response");
                return false;
            }

            applyPrinterStatus(printer, *received, uri);
            return true;
        }
    } // namespace callbacks

    // -----------------------------------------------------------------------
    bool updateDriverData(pappl_pr_driver_data_t *driverData,
                          const std::string_view &model)
    {
        if (!driverData)
            return false;

        const auto variant = modelVariantMap.at(model);
        const std::string makeAndModel = std::format("Brother {}", model);

        driverData->printfile_cb  = callbacks::print;
        driverData->rendjob_cb    = callbacks::endJob;
        driverData->rendpage_cb   = callbacks::endPage;
        driverData->rstartjob_cb  = callbacks::startJob;
        driverData->rstartpage_cb = callbacks::startPage;
        driverData->rwriteline_cb = callbacks::writeLine;
        driverData->status_cb     = callbacks::getStatus;
        driverData->output_face_up = true;
        driverData->input_face_up  = true;

        driverData->kind = PAPPL_KIND_LABEL;

        papplCopyString(driverData->make_and_model,
                        makeAndModel.c_str(),
                        sizeof(driverData->make_and_model));

        driverData->ppm = 1;

        driverData->orient_default  = IPP_ORIENT_PORTRAIT;
        driverData->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->color_default   = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->content_default = PAPPL_CONTENT_TEXT;
        driverData->quality_default = IPP_QUALITY_NORMAL;

        driverData->raster_types      = PAPPL_PWG_RASTER_TYPE_BLACK_1;
        driverData->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;

        driverData->duplex          = PAPPL_DUPLEX_NONE;
        driverData->sides_supported = PAPPL_SIDES_ONE_SIDED;
        driverData->sides_default   = PAPPL_SIDES_ONE_SIDED;
        driverData->finishings      = PAPPL_FINISHINGS_NONE;

        // Only advertise 180x180. The "360 dpi" in the spec (§2.3.1) refers to
        // the feed direction only; the cross-tape direction is always 128 pins
        // regardless of resolution. Advertising x=360 causes PAPPL to render
        // cupsWidth=340 pixels which breaks the 16-byte line extraction.
        driverData->num_resolution  = 1;
        driverData->x_resolution[0] = 180;
        driverData->y_resolution[0] = 180;
        driverData->x_default       = 180;
        driverData->y_default       = 180;

        driverData->left_right = 0;
        driverData->bottom_top = 0;

        driverData->num_source = 2;
        driverData->source[0]  = "main-roll";
        driverData->source[1]  = "auto";

        driverData->num_type = static_cast<int>(mediaTypes.size());
        std::ranges::copy(mediaTypes, driverData->type);

        std::vector<const char *> media{defaultMedia.begin(), defaultMedia.end()};
        driverData->num_media = static_cast<int>(media.size());
        std::ranges::copy(media, driverData->media);

        if (driverData->num_media > 0)
        {
            papplCopyString(driverData->media_ready[0].size_name,
                            defaultMedia[0],
                            sizeof(driverData->media_ready[0].size_name));

            if (const pwg_media_t *pwg = pwgMediaForPWG(driverData->media_ready[0].size_name))
            {
                driverData->media_ready[0].size_width  = pwg->width;
                driverData->media_ready[0].size_length = pwg->length;
                driverData->media_ready[0].bottom_margin = margins.at(variant).bottom;
                driverData->media_ready[0].left_margin   = margins.at(variant).left;
                driverData->media_ready[0].right_margin  = margins.at(variant).right;
                driverData->media_ready[0].top_margin    = margins.at(variant).top;
                papplCopyString(driverData->media_ready[0].source,
                                driverData->source[0],
                                sizeof(driverData->media_ready[0].source));
                papplCopyString(driverData->media_ready[0].type,
                                driverData->type[0],
                                sizeof(driverData->media_ready[0].type));
            }
            else
            {
                return false;
            }

            papplCopyString(driverData->media_default.size_name,
                            defaultMedia[0],
                            sizeof(driverData->media_default.size_name));

            if (const pwg_media_t *pwg = pwgMediaForPWG(driverData->media_default.size_name))
            {
                driverData->media_default.size_width  = pwg->width;
                driverData->media_default.size_length = pwg->length;
                driverData->media_default.bottom_margin = margins.at(variant).bottom;
                driverData->media_default.left_margin   = margins.at(variant).left;
                driverData->media_default.right_margin  = margins.at(variant).right;
                driverData->media_default.top_margin    = margins.at(variant).top;
                papplCopyString(driverData->media_default.type,
                                driverData->type[0],
                                sizeof(driverData->media_default.type));
            }
            else
            {
                return false;
            }
        }

        return true;
    }

} // namespace drivers::pte550w
