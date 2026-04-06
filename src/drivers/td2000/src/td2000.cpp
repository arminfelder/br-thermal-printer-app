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

#include <memory>
#include <optional>
#include <span>
#include <thread>

extern "C" {
#include <pappl/pappl.h>
#include <pappl/base.h>
}

#include "util.h"

namespace drivers::td2000
{
    // Attempts one read from device and returns the status packet if valid.
    // Spec §4 p.21-26 (ESC i S): status response is always 32 bytes; byte 0 (printHeadMark)
    // is fixed at 80h. Returns nullopt when no complete valid packet arrived.
    static std::optional<types::PrinterInfo> readStatusPacket(pappl_device_t *device)
    {
        types::PrinterInfo info{};
        const auto result = papplDeviceRead(device, info.raw.data(), info.raw.size());
        if (result == static_cast<ssize_t>(info.raw.size()) &&
            info.fields().printHeadMark == 0x80)
        {
            return info;
        }
        return std::nullopt;
    }

    namespace callbacks
    {
        bool print(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            return true;
        }

        bool startJob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
        {
            if (!job || !options || !device)
            {
                return false;
            }

            const auto jobId = papplJobGetID(job);

            std::array<char, 128> deviceId{};
            if (papplDeviceGetID(device, deviceId.data(), deviceId.size()) == nullptr)
            {
                return false;
            }

            cups_option_t *did = nullptr;
            const int numOptions = papplDeviceParseID(deviceId.data(), &did);
            const char *mdl = cupsGetOption("MDL", numOptions, did);
            const std::string model = mdl ? mdl : "";
            cupsFreeOptions(numOptions, did);

            if (model.empty())
            {
                return false;
            }
            const auto familyIt = modelFamilyMap.find(model);
            if (familyIt == modelFamilyMap.end())
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unknown model: %s", model.c_str());
                return false;
            }
            const auto modelFamily = familyIt->second;

            auto jobData = std::make_unique<types::JobData>(types::JobData{
                .modelFamily = modelFamily,
                .useCompression = false,
                .bytesPerLine = lineWidth.at(modelFamily),
            });

            // Spec §2.1 p.5, §4 p.21: Invalidate (NULL × 200) — clears the print buffer
            // and resets the printer to the receiving state.
            if (const std::vector<uint8_t> data(200, 0); !util::writeToDevice(data, device, jobId))
            {
                return false;
            }

            // Spec §4 p.21: ESC @ (1Bh 40h) — Initialize; resets all mode settings.
            // Also used to cancel a print job mid-transmission.
            const commands::Initialize initialize;
            if (!util::writeToDevice(initialize.get(), device, jobId))
            {
                return false;
            }

            // Spec §4 p.27: ESC i a (1Bh 69h 61h 01h) — Switch dynamic command mode;
            // n=1 selects raster mode. Must precede all raster data.
            const commands::SwitchDynamicCommandMode switchDynamicCommandMode{types::CommandMode::Raster};
            if (!util::writeToDevice(switchDynamicCommandMode.get(), device, jobId))
            {
                return false;
            }

            // Spec §1 p.3-4, §4 p.21 (ESC i S note): once print data transmission begins
            // the printer pushes status packets automatically — do not send ESC i S.
            // Mark push mode so getStatus does not issue a status request command.
            const auto printer = papplJobGetPrinter(job);
            const auto uri = papplPrinterGetDeviceURI(printer);
            {
                std::lock_guard lock(printerStateMutex);
                printerStatusPolling[uri] = true;
            }

            papplJobSetData(job, jobData.release()); // ownership transferred to the job
            return true;
        }

        bool startPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            // Determine media type from IPP media-type string
            types::MediaType mediaType{};
            const std::string_view mediaTypeStr(options->media.type);
            if (mediaTypeStr == "labels-continuous" || mediaTypeStr == "continuous")
            {
                mediaType = types::MediaType::ContinuousLengthTape;
            }
            else if (mediaTypeStr == "labels")
            {
                mediaType = types::MediaType::DieCutLabels;
            }
            else
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "unsupported media type: %s", options->media.type);
                return false;
            }

            const auto jobData = static_cast<types::JobData*>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }

            // Spec §2.1 p.5: per-page control-code order:
            //   1. AdditionalMediaInformation  (ESC i U)
            //   2. PrintInformation            (ESC i z)
            //   3. VariousModeSettings         (ESC i M)
            //   4. SpecifyMarginAmount         (ESC i d)
            //   5. SelectCompressionMode       (M)

            // 1. Spec §4 p.27-28: ESC i U (1Bh 69h 55h 77h 01h + 127 bytes) —
            //    Additional media information command; updates the printer's media info.
            //    May be skipped if media has not changed since the last print.
            const auto mediaInfo = media::getMediaInfoForMedia(jobData->modelFamily, options->media.size_width, options->media.size_length, mediaType);
            if (mediaInfo == media::none)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unsupported media dimensions for model family");
                return false;
            }
            const commands::AdditionalMediaInformation additionalMediaInfo{mediaInfo};
            if (!util::writeToDevice(additionalMediaInfo.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send additionalMediaInfo");
                return false;
            }

            // 2. Spec §4 p.29: ESC i z (1Bh 69h 7Ah + 10 bytes) — Print information command.
            //    {n1} valid flags: PI_KIND=0x02 (media type), PI_WIDTH=0x04, PI_LENGTH=0x08,
            //                      PI_RECOVER=0x80 (printer recovery on)
            //    {n2} media type: 0x0A=continuous length tape, 0x0B=die-cut labels
            //    {n3}/{n4} media width/length in mm
            //    {n5-n8} raster line count, little-endian: count = n8*256³+n7*256²+n6*256+n5
            //    {n9} page type: 0=starting page, 1=other pages
            //    {n10} fixed at 0
            types::PrintInfoFields info{};
            info.mediaType = static_cast<uint8_t>(mediaType);
            info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) |
                               static_cast<uint8_t>(types::PrintInfoFlags::pi_width) |
                               static_cast<uint8_t>(types::PrintInfoFlags::pi_recover) |
                               static_cast<uint8_t>(types::PrintInfoFlags::pi_length);
            info.pageType = (pageNumber == 0) ? types::PageType::startingPage : types::PageType::otherPage;
            info.mediaWidth  = static_cast<uint8_t>(options->media.size_width  / 100);
            info.mediaLength = static_cast<uint8_t>(options->media.size_length / 100);
            // Raster number is 4 bytes little-endian (n5=LSB, n8=MSB).
            const auto rasterLineCount = static_cast<uint32_t>(options->header.cupsHeight);
            const auto rasterBytes = std::bit_cast<std::array<uint8_t, 4>>(rasterLineCount);
            std::ranges::copy(rasterBytes, info.rasterNumber);

            const commands::PrintInformation printInfo{info};
            if (!util::writeToDevice(printInfo.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send printInfo");
                return false;
            }

            // 3. Spec §4 p.30: ESC i M (1Bh 69h 4Dh {n}) — Various mode settings.
            //    Bit 3: rotate 180°; Bit 4: peeler function. All others unused.
            constexpr types::VariousModeSettings settings{};
            const commands::VariousModeSettings variousModeSettings{settings};
            if (!util::writeToDevice(variousModeSettings.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send variousModeSettings");
                return false;
            }

            // 4. Spec §4 p.30: ESC i d (1Bh 69h 64h {n1} {n2}) — Specify margin amount.
            //    margin(dots) = n1 + n2*256. Die-cut labels must use 0 ("only 0 is available").
            const uint8_t feedMargin = (mediaType == types::MediaType::ContinuousLengthTape)
                                       ? minFeedMarginDots.at(jobData->modelFamily) : 0;
            const commands::SpecifyMarginAmount specifyMargin(feedMargin, 0);
            if (!util::writeToDevice(specifyMargin.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send specifyMargin");
                return false;
            }

            // 5. Spec §4 p.31: M (4Dh {n}) — Select compression mode.
            //    n=0: no compression; n=2: TIFF (PackBits). Only applies to raster data.
            const commands::SelectCompressionMode compressionModeCommand{types::CompressionMode::NoCompression};
            if (!util::writeToDevice(compressionModeCommand.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send compressionModeCommand");
                return false;
            }

            return true;
        }

        // Maps a received PrinterInfo packet to PAPPL printer reasons.
        // Replaces all previously set device-status reasons on every call.
        void applyPrinterStatus(pappl_printer_t *printer, const types::PrinterInfo &info,
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
                if (fields.errorInformation1.noMedia || fields.errorInformation1.endOfMedia)
                    status |= PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED;
                if (fields.errorInformation2.replacingMedia)
                    status |= PAPPL_PREASON_MEDIA_NEEDED;
                if (fields.errorInformation2.mediaCannotBeFed)
                    status |= PAPPL_PREASON_MEDIA_JAM | PAPPL_PREASON_MEDIA_EMPTY;
                if (fields.errorInformation1.printerInUse ||
                    fields.errorInformation2.systemError ||
                    fields.errorInformation2.communicationError)
                    status |= PAPPL_PREASON_OTHER;
                papplPrinterSetReasons(printer, static_cast<pappl_preason_t>(status),
                                       PAPPL_PREASON_DEVICE_STATUS);
            }
            else if (fields.statusType == types::StatusType::TurnedOff)
            {
                papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE,
                                       PAPPL_PREASON_DEVICE_STATUS);
            }
            else if (fields.statusType == types::StatusType::Notification)
            {
                if (fields.notificationNumber == types::NotificationNumber::CoolingStarted ||
                    fields.notificationNumber == types::NotificationNumber::WaitingForPeeling ||
                    fields.notificationNumber == types::NotificationNumber::PrinterPaused)
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
                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_DEVICE_STATUS);
            }
        }

        // Reads from device until the printer signals it has resumed after cooling or a pause.
        // Spec §5.4 p.38 (cooling flow), §5.5 p.39 (peeling flow): during cooling the printer
        // returns zero-length USB packets; a Notification packet with CoolingFinished (04h) or
        // FinishedPrinterPaused (08h) signals that data transmission may resume.
        bool waitForPrinterResume(pappl_job_t *job, pappl_device_t *device,
                                   pappl_printer_t *printer, const char *uri)
        {
            constexpr int kMaxWaitMs = 120'000; // 2 minutes
            constexpr int kDelayMs   = 100;

            for (int waited = 0; waited < kMaxWaitMs; waited += kDelayMs)
            {
                if (const auto packet = readStatusPacket(device))
                {
                    const auto fields = packet->fields();
                    applyPrinterStatus(printer, *packet, uri);
                    if (fields.statusType == types::StatusType::Notification &&
                        (fields.notificationNumber == types::NotificationNumber::CoolingFinished ||
                         fields.notificationNumber == types::NotificationNumber::FinishedPrinterPaused))
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
                    // Other packets (phase changes, etc.): keep waiting
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs));
            }
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Timed out waiting for printer to resume");
            return false;
        }

        bool writeLine(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, [[maybe_unused]] unsigned pageNumber, const unsigned char *pixels)
        {
            if (!pixels || !options || !job || !device)
            {
                return false;
            }
            const auto jobData = static_cast<types::JobData*>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }

            const auto bytesPerLine = jobData->bytesPerLine;
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            const std::span lineData(pixels, static_cast<size_t>(bytesPerLine));
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif

            const auto mirroredLine = util::mirrorLine(lineData);

            // Spec §4 p.33: g (67h 00h {n} {d1..dn}) — Raster graphics transfer.
            // {n} = number of bytes: 56 for Td2x2x (203 dpi), 84 for Td2x3x (300 dpi)
            // when no compression is selected. With TIFF compression the byte count
            // reflects the compressed payload size.
            std::vector<uint8_t> rasterCmd;
            if (!jobData->useCompression)
            {
                const commands::RasterGraphicsTransfer rgt{bytesPerLine, mirroredLine};
                rasterCmd = rgt.get();
            }
            else
            {
                const auto compressed = util::compressLine(mirroredLine);
                const commands::RasterGraphicsTransfer rgt{static_cast<int>(lineData.size()), compressed};
                rasterCmd = rgt.get();
            }

            const auto printer = papplJobGetPrinter(job);
            const auto uri     = papplPrinterGetDeviceURI(printer);

            // Write with one cooling/pause retry (spec §5 p.34, §5.4 p.38 cooling flow,
            // §5.5 p.39 peeling flow). papplDeviceWrite is used directly so a transient
            // write failure (USB NAK during cooling) does not close the device.
            for (int retry = 0; retry <= 1; ++retry)
            {
                const auto written = papplDeviceWrite(device, rasterCmd.data(), rasterCmd.size());
                if (written == static_cast<ssize_t>(rasterCmd.size()))
                {
                    papplDeviceFlush(device);

                    // Single non-blocking read: pick up any pushed status packet
                    // (phase change, error, or notification sent during transmission).
                    if (const auto packet = readStatusPacket(device))
                    {
                        const auto fields = packet->fields();
                        applyPrinterStatus(printer, *packet, uri);

                        if (fields.statusType == types::StatusType::ErrorOccurred)
                        {
                            papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                        "Printer error during raster transmission");
                            return false;
                        }
                        // Cooling or pause that started after a successful write:
                        // the line was accepted — wait for resume before the next line.
                        if (fields.statusType == types::StatusType::Notification &&
                            (fields.notificationNumber == types::NotificationNumber::CoolingStarted ||
                             fields.notificationNumber == types::NotificationNumber::PrinterPaused))
                        {
                            papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                                        "Printer cooling/paused after write, waiting to resume");
                            if (!waitForPrinterResume(job, device, printer, uri))
                                return false;
                        }
                    }
                    return true;
                }

                if (retry >= 1) break;

                // Write failed — printer likely NAK'd during cooling (spec §5.4 p.38).
                // Read status to confirm the cause and wait for resume before retrying.
                papplLogJob(job, PAPPL_LOGLEVEL_WARN,
                            "Raster write failed, checking for cooling or pause");

                std::optional<types::PrinterInfo> statusPacket;
                constexpr int kStatusAttempts = 20; // 20 × 50 ms = 1 s
                for (int attempt = 0; attempt < kStatusAttempts; ++attempt)
                {
                    statusPacket = readStatusPacket(device);
                    if (statusPacket) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (!statusPacket)
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                "Write failed and could not read printer status");
                    return false;
                }

                const auto fields = statusPacket->fields();
                applyPrinterStatus(printer, *statusPacket, uri);

                if (fields.statusType == types::StatusType::Notification &&
                    (fields.notificationNumber == types::NotificationNumber::CoolingStarted ||
                     fields.notificationNumber == types::NotificationNumber::PrinterPaused))
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                                "Printer cooling/paused, waiting to resume before retry");
                    if (!waitForPrinterResume(job, device, printer, uri))
                        return false;
                    // Fall through to retry the write
                }
                else
                {
                    return false; // ErrorOccurred or unexpected failure
                }
            }

            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to write raster line after retry");
            return false;
        }

        bool endPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, const unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }

            if (const auto pageCount = static_cast<unsigned>(papplJobGetImpressions(job)); pageNumber < pageCount - 1)
            {
                // Spec §4 p.33: FF (0Ch) — Print command; used at the end of every page
                // except the last in a multi-page job.
                const commands::Print print;
                if (const auto job_id = papplJobGetID(job); !util::writeToDevice(print.get(), device, job_id))
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send print command");
                    return false;
                }
            }

            return true;
        }

        bool endJob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            // Spec §4 p.33: Control-Z (1Ah) — Print command with feeding; used at the
            // end of the last page to trigger printing and media feed.
            const commands::PrintWithFeeding printWithFeeding;
            if (!util::writeToDevice(printWithFeeding.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send printWithFeeding command");
                return false;
            }

            // Spec §5.1 p.35 (concurrent printing normal flow): after the final print
            // command the printer pushes PhaseChange "Printing" (06h/01h), then
            // PrintingCompleted (01h), then PhaseChange "Waiting to receive" (06h/00h).
            // Drain these so the device is clean for the next job and end-of-job
            // errors are captured.
            constexpr int kMaxAttempts = 200; // 200 × 20 ms = 4 s timeout
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                if (const auto packet = readStatusPacket(device))
                {
                    const auto fields = packet->fields();
                    if (fields.statusType == types::StatusType::PhaseChange &&
                        fields.phaseType == types::Phase::ReceivingState)
                    {
                        break; // Printer back to receiving state — done
                    }
                    if (fields.statusType == types::StatusType::ErrorOccurred)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                                    "Printer reported error during job completion");
                        break;
                    }
                    // PhaseChange "Printing", PrintingCompleted, Notification: keep reading
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // Return to pull mode so getStatus can send ESC i S again
            const auto printer = papplJobGetPrinter(job);
            const auto uri = papplPrinterGetDeviceURI(printer);
            {
                std::lock_guard lock(printerStateMutex);
                printerStatusPolling[uri] = false;
            }

            const auto jobData = static_cast<types::JobData *>(papplJobGetData(job));
            papplJobSetData(job, nullptr);
            delete jobData;

            return true;
        }

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
                // Spec §4 p.21 (ESC i S note): "do not send this command while printing;
                // error information is automatically sent by the printer during printing."
                // Attempt one non-blocking read to pick up any pushed packet (phase change,
                // error, or notification). Spec §4 p.25 (table 6): phase statuses are sent
                // as PhaseChange (06h) packets with PhaseType printing(01h)/receiving(00h).
                const auto device = papplPrinterOpenDevice(printer);
                if (!device)
                {
                    return true; // Device unavailable — keep current reasons
                }

                if (const auto packet = readStatusPacket(device))
                {
                    applyPrinterStatus(printer, *packet, uri);
                }
                papplPrinterCloseDevice(printer);
                return true;
            }

            // Pull mode: Spec §4 p.21: ESC i S (1Bh 69h 53h) — Status information request.
            // Response is 32 bytes; byte 0 (printHeadMark) fixed at 80h (spec §4 p.22).
            const auto device = papplPrinterOpenDevice(printer);
            if (!device)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN,
                                "getStatus: could not open device");
                return false;
            }

            const commands::StatusInformationRequest cmd;
            if (!util::writeToDevice(cmd.get(), device, -1))
            {
                papplPrinterCloseDevice(printer);
                return false;
            }

            std::optional<types::PrinterInfo> received;
            constexpr int kMaxAttempts = 100; // 100 × 10 ms = 1 s timeout
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                received = readStatusPacket(device);
                if (received) break;

                // readStatusPacket returns nullopt both on short reads and on read errors;
                // check for a hard error by re-reading with a fresh buffer
                types::PrinterInfo probe{};
                if (papplDeviceRead(device, probe.raw.data(), probe.raw.size()) < 0)
                {
                    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to read status");
                    papplPrinterCloseDevice(printer);
                    papplPrinterSetReasons(printer,
                                          PAPPL_PREASON_OTHER | PAPPL_PREASON_OFFLINE,
                                          PAPPL_PREASON_DEVICE_STATUS);
                    return false;
                }
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
    }

    namespace media
    {
        types::MediaInfo getMediaInfoForMedia(const types::ModelFamily family, const int width, const int length, const types::MediaType type) noexcept
        {
            //TODO: use media information from the printer instead of static data
            auto pick = [&](const types::MediaInfo& td2x2x_val, const types::MediaInfo& td2x3x_val) -> types::MediaInfo {
                return (family == types::ModelFamily::Td2x3x) ? td2x3x_val : td2x2x_val;
            };

            switch (type)
            {
            case types::MediaType::ContinuousLengthTape:
                return (width <= 5700) ? pick(td2x2x::_57mm, td2x3x::_57mm)
                                       : pick(td2x2x::_58mm, td2x3x::_58mm);

            case types::MediaType::DieCutLabels:
                if (width <= 4000)
                {
                    if (length <= 4000) return pick(td2x2x::_40x40mm, td2x3x::_40x40mm);
                    if (length <= 5000) return pick(td2x2x::_40x50mm, td2x3x::_40x50mm);
                    if (length <= 6000) return pick(td2x2x::_40x60mm, td2x3x::_40x60mm);
                }
                else if (width <= 5000) return pick(td2x2x::_50x30mm, td2x3x::_50x30mm);
                else if (width <= 5100) return pick(td2x2x::_51x26mm, td2x3x::_51x26mm);
                else if (width <= 6000) return pick(td2x2x::_60x60mm, td2x3x::_60x60mm);
                return none; // dimensions outside handled ranges

            case types::MediaType::NoMedia:
            default:
                return none;
            }
        }
    }

    bool updateDriverData(pappl_pr_driver_data_t* driverData, const std::string_view& model)
    {
        if (!driverData)
        {
            return false;
        }

        const auto family = modelFamilyMap.at(model);
        const std::string makeAndModel = std::format("Brother {}", model);

        std::vector<const char *> media{defaultMedia.begin(), defaultMedia.end()};

        driverData->printfile_cb = callbacks::print;
        driverData->rendjob_cb = callbacks::endJob;
        driverData->rendpage_cb = callbacks::endPage;
        driverData->rstartjob_cb = callbacks::startJob;
        driverData->rstartpage_cb = callbacks::startPage;
        driverData->rwriteline_cb = callbacks::writeLine;
        driverData->status_cb = callbacks::getStatus;
        driverData->output_face_up = true;
        driverData->input_face_up = true;

        driverData->kind = PAPPL_KIND_RECEIPT | PAPPL_KIND_LABEL;

        papplCopyString(driverData->make_and_model, makeAndModel.c_str(), sizeof(driverData->make_and_model));

        driverData->ppm = 1;

        driverData->orient_default = IPP_ORIENT_PORTRAIT;
        driverData->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->color_default = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->content_default = PAPPL_CONTENT_TEXT;
        driverData->quality_default = IPP_QUALITY_NORMAL;

        driverData->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1;
        driverData->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;

        driverData->duplex = PAPPL_DUPLEX_NONE;
        driverData->sides_supported = PAPPL_SIDES_ONE_SIDED;
        driverData->sides_default = PAPPL_SIDES_ONE_SIDED;
        driverData->finishings = PAPPL_FINISHINGS_NONE;

        driverData->num_resolution = 1;

        driverData->x_resolution[0] = resolutions.at(family).x;
        driverData->y_resolution[0] = resolutions.at(family).y;

        driverData->x_default = resolutions.at(family).x;
        driverData->y_default = resolutions.at(family).y;

        driverData->left_right = 0;
        driverData->bottom_top = 0;

        driverData->num_source = 1;
        driverData->source[0] = "main-roll";

        driverData->num_type = static_cast<int>(mediaTypes.size());
        std::ranges::copy(mediaTypes, driverData->type);

        driverData->num_media = static_cast<int>(media.size());
        std::ranges::copy(media, driverData->media);

        if (driverData->num_media > 0)
        {
            constexpr std::string_view readyMedia = "roll_current_58x0mm";
            papplCopyString(driverData->media_ready[0].size_name, readyMedia.data(), sizeof(driverData->media_ready[0].size_name));
            if (const pwg_media_t *pwg = pwgMediaForPWG(driverData->media_ready[0].size_name))
            {
                driverData->media_ready[0].size_width  = pwg->width;
                driverData->media_ready[0].size_length = pwg->length;
                driverData->media_ready[0].bottom_margin = margins.at(family).bottom;
                driverData->media_ready[0].left_margin   = margins.at(family).left;
                driverData->media_ready[0].right_margin  = margins.at(family).right;
                driverData->media_ready[0].top_margin    = margins.at(family).top;

                papplCopyString(driverData->media_ready[0].source, driverData->source[0], sizeof(driverData->media_ready[0].source));
                papplCopyString(driverData->media_ready[0].type,   driverData->type[0],   sizeof(driverData->media_ready[0].type));
            }
            else
            {
                return false;
            }

            papplCopyString(driverData->media_default.size_name, defaultMedia[0], sizeof(driverData->media_default.size_name));
            if (const pwg_media_t *pwg = pwgMediaForPWG(driverData->media_default.size_name))
            {
                driverData->media_default.size_width  = pwg->width;
                driverData->media_default.size_length = pwg->length;
                driverData->media_default.bottom_margin = margins.at(family).bottom;
                driverData->media_default.left_margin   = margins.at(family).left;
                driverData->media_default.right_margin  = margins.at(family).right;
                driverData->media_default.top_margin    = margins.at(family).top;
                papplCopyString(driverData->media_default.type, driverData->type[0], sizeof(driverData->media_default.type));

                papplCopyString(driverData->media_ready[0].source, driverData->source[0], sizeof(driverData->media_ready[0].source));
                papplCopyString(driverData->media_ready[0].type,   driverData->type[0],   sizeof(driverData->media_ready[0].type));
            }
            else
            {
                return false;
            }
        }

        return true;
    }
}
