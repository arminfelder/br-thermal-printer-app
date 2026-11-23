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

// The PAPPL raster callbacks for a Brother raster printer. The job flow, the status
// handling and the cooling retry are the same for every family. A driver supplies a
// Traits type with the model-specific parts:
//
//   using Model;                       // the driver model key
//   using PrinterInfo;                 // a brother::raster::types::StatusBlock
//   using RasterGraphicsTransfer;      // a brother::raster::commands::RasterTransfer
//   static size_t invalidateBytes(Model);  // NULL bytes before ESC @
//   static constexpr bool   compressRaster;
//   static const Model* modelFor(std::string_view mdl);  // static storage, or nullptr
//   static int  headBytes(Model);
//   static bool acceptsMediaType(std::string_view);
//   static bool startJob(JobContext&);          // after ESC @
//   static bool startPage(PageContext&);        // after the FF of the previous page
//   static unsigned statusReasons(const PrinterInfo&);  // pappl_preason_t bits
//   static bool pausesPrinting(const PrinterInfo&);     // cooling or pause started
//   static bool resumesPrinting(const PrinterInfo&);    // cooling or pause finished
//   static bool pollsStatus(Model);             // false: no ESC i S when idle
//
// Optional:
//   static void onStatus(pappl_printer_t*, const PrinterInfo&);  // for each status block
//
// The RasterTraits concept below is the contract. The compiler checks it.

#ifndef BR_THERMAL_RASTER_PIPELINE_H
#define BR_THERMAL_RASTER_PIPELINE_H

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern "C" {
#include <pappl/pappl.h>
}

#include "brother_raster.h"
#include "util.h"

namespace brother::pipeline
{
    template <typename Model>
    struct JobData
    {
        Model model{};
        int   bytesPerLine = 0;
        // PAPPL keeps calling rwriteline_cb after a failure. Once set, every later
        // callback of this job returns false at once.
        bool failed = false;
        // Pages started by this job. PAPPL's page argument restarts at 0 for each
        // copy, thus it cannot drive the FF between pages.
        unsigned pagesStarted = 0;
        // Raster lines that the printer accepts for this page. 0 is no limit. The
        // driver sets it in startPage when the page is longer than the spec maximum.
        uint32_t lineLimit = 0;
        uint32_t linesSent = 0;

        // true when the next line of the page can go to the printer.
        bool takeLine() noexcept
        {
            if (lineLimit != 0 && linesSent >= lineLimit)
            {
                return false;
            }
            ++linesSent;
            return true;
        }
    };

    // Marks the job failed unless `ok` is set before the scope ends. The page and
    // line callbacks use it: every early return in them is fatal for the job.
    template <typename Data>
    struct FailUnlessOk
    {
        Data& job;
        bool ok = false;
        ~FailUnlessOk() { if (!ok) { job.failed = true; } }
    };

    template <typename Info>
    struct StatusRead
    {
        util::ReadResult result = util::ReadResult::NoData;
        Info info{};
    };

    // The read can block, see util::readStatusPacket.
    template <typename Info>
    StatusRead<Info> readStatus(pappl_device_t* device) noexcept
    {
        StatusRead<Info> read{};
        read.result = util::readStatusPacket(device, read.info);
        return read;
    }

    enum class JobStartStatus
    {
        Ready,
        PrinterError,   // the printer replied with status type "error occurred"
        DeviceError,    // the write or the read failed
    };

    // Raster spec flow chart 5.1: request the status before the print data. No reply is
    // not an error. `info` gets the reply when there is one.
    template <typename Info>
    JobStartStatus requestStatusAtJobStart(pappl_device_t* device, const int jobId, Info& info)
    {
        if (!util::writeToDevice(raster::commands::StatusInformationRequest{}.get(), device, jobId))
        {
            return JobStartStatus::DeviceError;
        }
        const auto read = readStatus<Info>(device);
        if (read.result == util::ReadResult::Error)
        {
            return JobStartStatus::DeviceError;
        }
        if (read.result != util::ReadResult::Packet)
        {
            return JobStartStatus::Ready;
        }
        info = read.info;
        return read.info.fields().statusType == raster::types::StatusType::ErrorOccurred
                   ? JobStartStatus::PrinterError
                   : JobStartStatus::Ready;
    }

    template <typename Model>
    struct JobContext
    {
        pappl_job_t*    job;
        pappl_device_t* device;
        int             jobId;
        Model           model;
    };

    // requestStatusAtJobStart plus the job log. false stops the job.
    template <typename Info, typename Model>
    bool checkStatusAtJobStart(const JobContext<Model>& ctx)
    {
        Info info{};
        const auto status = requestStatusAtJobStart(ctx.device, ctx.jobId, info);
        if (status == JobStartStatus::PrinterError)
        {
            papplLogJob(ctx.job, PAPPL_LOGLEVEL_ERROR, "Printer reported error 0x%02x 0x%02x",
                        static_cast<unsigned>(info.raw[8]), static_cast<unsigned>(info.raw[9]));
        }
        else if (status == JobStartStatus::DeviceError)
        {
            papplLogJob(ctx.job, PAPPL_LOGLEVEL_ERROR, "Status request failed at job start");
        }
        return status == JobStartStatus::Ready;
    }

    template <typename Model>
    struct PageContext
    {
        pappl_job_t*              job;
        pappl_pr_options_t*       options;
        pappl_device_t*           device;
        int                       jobId;
        JobData<Model>&           data;
        raster::types::PageType   pageType;
    };

    template <typename T>
    concept RasterTraits = requires(typename T::Model model, const typename T::PrinterInfo& info,
                                    const JobContext<typename T::Model>& jobContext,
                                    const PageContext<typename T::Model>& pageContext,
                                    std::string_view text)
    {
        typename T::RasterGraphicsTransfer;
        { T::compressRaster } -> std::convertible_to<bool>;
        { T::invalidateBytes(model) } -> std::convertible_to<std::size_t>;
        { T::modelFor(text) } -> std::same_as<const typename T::Model*>;
        { T::headBytes(model) } -> std::convertible_to<int>;
        { T::acceptsMediaType(text) } -> std::same_as<bool>;
        { T::startJob(jobContext) } -> std::same_as<bool>;
        { T::startPage(pageContext) } -> std::same_as<bool>;
        { T::statusReasons(info) } -> std::convertible_to<unsigned>;
        { T::pausesPrinting(info) } -> std::same_as<bool>;
        { T::resumesPrinting(info) } -> std::same_as<bool>;
        { T::pollsStatus(model) } -> std::same_as<bool>;
    };

    enum class JobEndStatus
    {
        Completed,     // the printer is in the receiving state again
        NoReply,       // the printer sent no status block
        Timeout,       // the printer sent status, but not the end of the print
        PrinterError,  // the printer sent status type "error occurred"
        DeviceError,   // the read failed
    };

    // The counts are reads that gave no data. Each such read is followed by `delay`.
    struct JobEndTiming
    {
        int idleReads     = 200;    // 200 x 20 ms = 4 s
        int printingReads = 6000;   // 6000 x 20 ms = 120 s
        std::chrono::milliseconds delay{20};
    };

    // Raster spec 5.1: after the last print command the printer sends PhaseChange
    // "printing", PrintingCompleted and PhaseChange "receiving". This function reads
    // these blocks and gives each block to `onStatus`.
    //
    // A printer that sends no block gets the short idle time only. After a "printing"
    // block the wait is the long time: ESC i S is not permitted while the printer prints,
    // thus the job must not end before the print ends.
    template <typename Info, std::invocable<const Info&> OnStatus>
    JobEndStatus drainJobEnd(pappl_device_t* device, OnStatus&& onStatus, const JobEndTiming timing = {})
    {
        using raster::types::Phase;
        using raster::types::StatusType;

        bool replied   = false;
        bool completed = false;
        int  remaining = timing.idleReads;
        while (remaining > 0)
        {
            const auto read = readStatus<Info>(device);
            if (read.result == util::ReadResult::Error)
            {
                return JobEndStatus::DeviceError;
            }
            if (read.result != util::ReadResult::Packet)
            {
                --remaining;
                std::this_thread::sleep_for(timing.delay);
                continue;
            }

            replied = true;
            onStatus(read.info);
            const auto fields = read.info.fields();
            if (fields.statusType == StatusType::ErrorOccurred)
            {
                return JobEndStatus::PrinterError;
            }
            if (fields.statusType == StatusType::PhaseChange)
            {
                if (fields.phaseType == Phase::ReceivingState)
                {
                    return JobEndStatus::Completed;
                }
                remaining = timing.printingReads;
            }
            else if (fields.statusType == StatusType::PrintingCompleted)
            {
                completed = true;
                remaining = timing.idleReads;
            }
        }
        if (completed)
        {
            return JobEndStatus::Completed;
        }
        return replied ? JobEndStatus::Timeout : JobEndStatus::NoReply;
    }

    template <RasterTraits Traits>
    class Pipeline
    {
    public:
        using Model       = typename Traits::Model;
        using PrinterInfo = typename Traits::PrinterInfo;
        using Data        = JobData<Model>;
        using Read        = StatusRead<PrinterInfo>;

        static bool print(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device)
        {
            return job && options && device;
        }

        static bool startJob(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            const auto* model = printerModel(papplJobGetPrinter(job));
            if (!model)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "The printer driver data has no model record");
                return false;
            }

            auto jobData = std::make_unique<Data>(Data{
                .model = *model,
                .bytesPerLine = Traits::headBytes(*model),
            });

            // Raster spec 2.1: the invalidate command clears the print buffer and resets
            // the printer to the receiving state.
            if (!util::writeToDevice(raster::commands::Invalidate{Traits::invalidateBytes(*model)}.get(), device, jobId))
            {
                return false;
            }
            // ESC @: initialize. It resets all mode settings.
            if (!util::writeToDevice(raster::commands::Initialize{}.get(), device, jobId))
            {
                return false;
            }

            JobContext<Model> const ctx{job, device, jobId, *model};
            if (!Traits::startJob(ctx))
            {
                return false;
            }

            // When print data starts, the printer sends status on its own. Thus getStatus
            // must not send ESC i S until endJob (raster spec 4, ESC i S note).
            setPushMode(papplPrinterGetDeviceURI(papplJobGetPrinter(job)), true);

            papplJobSetData(job, jobData.release()); // ownership transferred to the job
            return true;
        }

        static bool startPage(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device,
                              [[maybe_unused]] unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            const auto jobData = static_cast<Data*>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }
            if (jobData->failed)
            {
                return false;
            }
            FailUnlessOk<Data> latch{*jobData};

            if (!Traits::acceptsMediaType(options->media.type))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unsupported media type: %s", options->media.type);
                return false;
            }

            // FF (0Ch) ends every page except the last. It is sent here, before the next
            // page, thus it does not depend on a page count.
            if (jobData->pagesStarted > 0 &&
                !util::writeToDevice(raster::commands::Print{}.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send print command");
                return false;
            }
            const auto pageType = (jobData->pagesStarted == 0) ? raster::types::PageType::startingPage
                                                               : raster::types::PageType::otherPage;
            ++jobData->pagesStarted;
            jobData->lineLimit = 0;
            jobData->linesSent = 0;

            PageContext<Model> const ctx{job, options, device, jobId, *jobData, pageType};
            if (!Traits::startPage(ctx))
            {
                return false;
            }

            latch.ok = true;
            return true;
        }

        // Maps a status block to PAPPL printer reasons. Each call replaces all
        // device-status reasons that were set before.
        static void applyPrinterStatus(pappl_printer_t* printer, const PrinterInfo& info)
        {
            papplPrinterSetReasons(printer, static_cast<pappl_preason_t>(Traits::statusReasons(info)),
                                   PAPPL_PREASON_DEVICE_STATUS);
            if constexpr (requires { Traits::onStatus(printer, info); })
            {
                Traits::onStatus(printer, info);
            }
        }

        // TD raster spec 5.4 (cooling) and 5.5 (peeling): the printer stops and later sends
        // a notification that it continues.
        static bool waitForPrinterResume(pappl_job_t* job, pappl_device_t* device, pappl_printer_t* printer)
        {
            using raster::types::StatusType;

            constexpr int kMaxWaitMs = 120'000;
            constexpr int kDelayMs   = 100;

            for (int waited = 0; waited < kMaxWaitMs; waited += kDelayMs)
            {
                const auto read = readStatus<PrinterInfo>(device);
                if (read.result == util::ReadResult::Error)
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Device read failed while waiting to resume");
                    return false;
                }
                if (read.result == util::ReadResult::Packet)
                {
                    const auto fields = read.info.fields();
                    applyPrinterStatus(printer, read.info);
                    if (Traits::resumesPrinting(read.info))
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Printer resumed");
                        return true;
                    }
                    if (fields.statusType == StatusType::ErrorOccurred)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Printer error while waiting to resume");
                        return false;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs));
            }
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Timed out waiting for printer to resume");
            return false;
        }

        static bool writeLine(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device,
                              [[maybe_unused]] unsigned pageNumber, const unsigned char* pixels)
        {
            if (!pixels || !options || !job || !device)
            {
                return false;
            }
            const auto jobData = static_cast<Data*>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }
            if (jobData->failed)
            {
                return false;
            }
            FailUnlessOk<Data> latch{*jobData};

            // ESC i z declared the clamped line count. The printer must get no more lines.
            if (!jobData->takeLine())
            {
                latch.ok = true;
                return true;
            }

            const auto bytesPerLine = jobData->bytesPerLine;

            // Size the span from what PAPPL allocated, not from the head width. PAPPL
            // calculates cupsBytesPerLine from the media geometry, thus media that is
            // narrower than the head gives a shorter buffer. buildHeadLine clamps the line
            // and then pads it back to the head width.
            const auto deliveredBytes = static_cast<size_t>(options->header.cupsBytesPerLine);
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            const std::span lineData(pixels, deliveredBytes);
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif
            const auto headLine = util::buildHeadLine(
                lineData,
                static_cast<size_t>(options->header.cupsWidth),
                static_cast<size_t>(bytesPerLine) * 8);

            // With compression, the byte count is the size of the compressed payload.
            std::vector<uint8_t> rasterCmd;
            if constexpr (Traits::compressRaster)
            {
                const auto compressed = util::compressLine(headLine);
                rasterCmd = typename Traits::RasterGraphicsTransfer{static_cast<int>(compressed.size()), compressed}.get();
            }
            else
            {
                rasterCmd = typename Traits::RasterGraphicsTransfer{bytesPerLine, headLine}.get();
            }

            const auto printer = papplJobGetPrinter(job);

            // Write with one cooling or pause retry (raster spec 5, 5.4, 5.5). The write
            // uses papplDeviceWrite directly: a NAK during cooling is not fatal here.
            for (int retry = 0; retry <= 1; ++retry)
            {
                const auto written = papplDeviceWrite(device, rasterCmd.data(), rasterCmd.size());
                if (written == static_cast<ssize_t>(rasterCmd.size()))
                {
                    papplDeviceFlush(device);

                    // Get a status block that the printer sent during the transmission.
                    const auto read = readStatus<PrinterInfo>(device);
                    if (read.result == util::ReadResult::Error)
                    {
                        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Device read failed during raster transmission");
                        return false;
                    }
                    if (read.result == util::ReadResult::Packet)
                    {
                        applyPrinterStatus(printer, read.info);
                        if (read.info.fields().statusType == raster::types::StatusType::ErrorOccurred)
                        {
                            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Printer error during raster transmission");
                            return false;
                        }
                        // The printer accepted the line. Wait before the next line.
                        if (Traits::pausesPrinting(read.info))
                        {
                            papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                                        "Printer cooling/paused after write, waiting to resume");
                            if (!waitForPrinterResume(job, device, printer))
                            {
                                return false;
                            }
                        }
                    }
                    latch.ok = true;
                    return true;
                }

                if (retry >= 1)
                {
                    break;
                }

                // The write failed. Usually the printer sent a NAK because it cools (raster
                // spec 5.4). Read the status to find the cause, then wait before the retry.
                papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Raster write failed, checking for cooling or pause");

                Read statusPacket{};
                constexpr int kStatusAttempts = 20; // 20 x 50 ms = 1 s
                for (int attempt = 0; attempt < kStatusAttempts; ++attempt)
                {
                    statusPacket = readStatus<PrinterInfo>(device);
                    if (statusPacket.result != util::ReadResult::NoData)
                    {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (statusPacket.result != util::ReadResult::Packet)
                {
                    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Write failed and could not read printer status");
                    return false;
                }

                applyPrinterStatus(printer, statusPacket.info);
                if (!Traits::pausesPrinting(statusPacket.info))
                {
                    return false;
                }
                papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Printer cooling/paused, waiting to resume before retry");
                if (!waitForPrinterResume(job, device, printer))
                {
                    return false;
                }
            }

            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to write raster line after retry");
            return false;
        }

        // startPage sends the FF between pages, thus a page end sends nothing.
        // A false return makes PAPPL abort the job after a failed line.
        static bool endPage(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device,
                            [[maybe_unused]] unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobData = static_cast<Data*>(papplJobGetData(job));
            return jobData != nullptr && !jobData->failed;
        }

        static bool endJob(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            // Take the job data back first, thus every exit path below frees it and
            // returns the printer to pull mode.
            const std::unique_ptr<Data> jobData(static_cast<Data*>(papplJobGetData(job)));
            papplJobSetData(job, nullptr);

            const auto uri = papplPrinterGetDeviceURI(papplJobGetPrinter(job));
            const auto leavePushMode = [&] { setPushMode(uri, false); };

            if (!jobData || jobData->failed)
            {
                leavePushMode();
                return false;
            }

            // Control-Z (1Ah): print with feeding. It ends the last page.
            if (!util::writeToDevice(raster::commands::PrintWithFeeding{}.get(), device, jobId))
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send printWithFeeding command");
                leavePushMode();
                return false;
            }

            const auto printer = papplJobGetPrinter(job);
            const auto status = drainJobEnd<PrinterInfo>(
                device, [&](const PrinterInfo& info) { applyPrinterStatus(printer, info); });
            leavePushMode();

            switch (status)
            {
            case JobEndStatus::PrinterError:
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Printer reported error during job completion");
                return false;
            case JobEndStatus::DeviceError:
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Device read failed during job completion");
                return false;
            case JobEndStatus::Timeout:
                papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Printer did not report the end of the print");
                return true;
            case JobEndStatus::NoReply:
                papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Printer sent no status at job completion");
                return true;
            case JobEndStatus::Completed:
            default:
                return true;
            }
        }

        static bool getStatus(pappl_printer_t* printer)
        {
            const auto uri = papplPrinterGetDeviceURI(printer);

            if (inPushMode(uri))
            {
                // Raster spec 4, ESC i S: do not send it while the printer prints. Read one
                // block that the printer sent on its own.
                const auto device = papplPrinterOpenDevice(printer);
                if (!device)
                {
                    return true; // device unavailable: keep the current reasons
                }
                if (const auto read = readStatus<PrinterInfo>(device); read.result == util::ReadResult::Packet)
                {
                    applyPrinterStatus(printer, read.info);
                }
                papplPrinterCloseDevice(printer);
                return true;
            }

            if (const auto* model = printerModel(printer); !model || !Traits::pollsStatus(*model))
            {
                return true;
            }

            // Pull mode: ESC i S. The response is one 32-byte status block.
            const auto device = papplPrinterOpenDevice(printer);
            if (!device)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN, "getStatus: could not open device");
                return false;
            }

            if (!util::writeToDevice(raster::commands::StatusInformationRequest{}.get(), device, -1))
            {
                papplPrinterCloseDevice(printer);
                return false;
            }

            Read received{};
            constexpr int kMaxAttempts = 100; // 100 x 10 ms = 1 s
            for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
            {
                received = readStatus<PrinterInfo>(device);
                if (received.result != util::ReadResult::NoData)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            papplPrinterCloseDevice(printer);

            if (received.result == util::ReadResult::Error)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to read status");
                papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER | PAPPL_PREASON_OFFLINE,
                                       PAPPL_PREASON_DEVICE_STATUS);
                return false;
            }
            if (received.result != util::ReadResult::Packet)
            {
                papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN, "getStatus: timed out waiting for status response");
                return false;
            }

            applyPrinterStatus(printer, received.info);
            return true;
        }

        // Sets the PAPPL callbacks. Each callback that PAPPL calls goes through
        // util::guarded: an exception that leaves a callback ends the server.
        //
        // `model` must have static storage. PAPPL keeps the pointer in the driver data
        // and the callbacks read the model from there. PAPPL does not free it.
        static void install(pappl_pr_driver_data_t* driverData, const Model& model) noexcept
        {
            driverData->extension     = const_cast<Model*>(&model);
            driverData->delete_cb     = nullptr;
            driverData->printfile_cb  = print;
            driverData->rstartjob_cb  = guard::startJob;
            driverData->rstartpage_cb = guard::startPage;
            driverData->rwriteline_cb = guard::writeLine;
            driverData->rendpage_cb   = guard::endPage;
            driverData->rendjob_cb    = guard::endJob;
            driverData->status_cb     = guard::getStatus;
        }

        // The model that install() put into the driver data of `printer`, or nullptr.
        static const Model* printerModel(pappl_printer_t* printer)
        {
            if (!printer)
            {
                return nullptr;
            }
            const auto data = std::make_unique<pappl_pr_driver_data_t>();
            if (!papplPrinterGetDriverData(printer, data.get()))
            {
                return nullptr;
            }
            return static_cast<const Model*>(data->extension);
        }

    private:
        // One map per driver. The key is the device URI.
        inline static std::map<std::string, bool> pushMode{};
        inline static std::mutex stateMutex{};

        static void setPushMode(const char* uri, const bool on)
        {
            std::lock_guard const lock(stateMutex);
            pushMode[uri ? uri : ""] = on;
        }

        static bool inPushMode(const char* uri)
        {
            std::lock_guard const lock(stateMutex);
            const auto it = pushMode.find(uri ? uri : "");
            return it != pushMode.end() && it->second;
        }

        struct guard
        {
            static bool startJob(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device)
            {
                return util::guarded(job, [&] { return Pipeline::startJob(job, options, device); });
            }
            static bool startPage(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device, unsigned page)
            {
                return util::guarded(job, [&] { return Pipeline::startPage(job, options, device, page); });
            }
            static bool writeLine(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device, unsigned page,
                                  const unsigned char* pixels)
            {
                return util::guarded(job, [&] { return Pipeline::writeLine(job, options, device, page, pixels); });
            }
            static bool endPage(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device, unsigned page)
            {
                return util::guarded(job, [&] { return Pipeline::endPage(job, options, device, page); });
            }
            static bool endJob(pappl_job_t* job, pappl_pr_options_t* options, pappl_device_t* device)
            {
                return util::guarded(job, [&] { return Pipeline::endJob(job, options, device); });
            }
            static bool getStatus(pappl_printer_t* printer)
            {
                return util::guarded(printer, [&] { return Pipeline::getStatus(printer); });
            }
        };
    };
}

#endif // BR_THERMAL_RASTER_PIPELINE_H
