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
#include <span>
#include <thread>

extern "C" {
#include <pappl/pappl.h>
#include <pappl/base.h>
}

#include "td2000.h"
#include "util.h"

using namespace drivers::td2000;

namespace drivers::td2000
{
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
            if (const auto result = papplDeviceGetID(device,deviceId.data(),deviceId.size()); result == nullptr)
            {
                return false;
            }

            cups_option_t *did = nullptr;
            const int numOptions = papplDeviceParseID(deviceId.data(), &did);

            const std::string_view model(cupsGetOption("MDL", numOptions, did));
            if (model.empty())
            {
                return false;
            }
            const auto modelFamily = modelFamilyMap.at(model);

            // ReSharper disable once CppDFAMemoryLeak
            const auto jobData = new types::JobData{
                .modelFamily = modelFamily,
                .useCompression = false,
                .bytesPerLine = lineWidth.at(modelFamily),
            };

            papplJobSetData(job, jobData);

            //statusPushMode = true;

            //invalidate
            if (const std::vector<uint8_t> data(200, 0); util::writeToDevice(data,device, jobId ) != true)
            {
                papplJobSetData(job, nullptr);
                delete jobData;
                return false;
            }

            const commands::Initialize initialize;
            if (util::writeToDevice(initialize.get(), device, jobId) != true)
            {
                papplJobSetData(job, nullptr);
                delete jobData;
                return false;
            }

            const commands::SwitchDynamicCommandMode switchDynamicCommandMode{types::CommandMode::Raster};
            if (util::writeToDevice(switchDynamicCommandMode.get(), device, jobId) != true)
            {
                papplJobSetData(job, nullptr);
                delete jobData;
                return false;
            }

            return true;
        }

        bool startPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }
            const auto jobId = papplJobGetID(job);

            types::PrintInfoFields info{};

            types::MediaType mediaType{};
            if ((std::string_view(options->media.type) == "continuous") == 0)
            {
                mediaType = types::MediaType::ContinuousLengthTape;

            }else if ((std::string_view(options->media.type) == "continuous-label") == 0)
            {
                mediaType = types::MediaType::DieCutLabels;
            } else
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "unsupported media type:  %s", options->media.type);
                return false;
            }

            info.mediaType = static_cast<uint8_t>(mediaType);
            info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) | static_cast<uint8_t>(
                types::PrintInfoFlags::pi_width) | static_cast<uint8_t>(types::PrintInfoFlags::pi_recover) | static_cast<uint8_t>(types::PrintInfoFlags::pi_length);

            info.pageType = (pageNumber == 0) ? types::PageType::startingPage : types::PageType::otherPage;

            info.mediaWidth = static_cast<uint8_t>(options->media.size_width/100);
            info.mediaLength = static_cast<uint8_t>(options->media.size_length/100);

            const auto rasterLineCount = options->header.cupsHeight;
            //
            info.rasterNumber[0] = static_cast<uint8_t>(rasterLineCount);
            info.rasterNumber[1] = static_cast<uint8_t>((rasterLineCount >> 8) & 0xFF);
            info.rasterNumber[2] = static_cast<uint8_t>((rasterLineCount >> 16) & 0xFF);
            info.rasterNumber[3] = static_cast<uint8_t>((rasterLineCount >> 24) & 0xFF);

            const commands::PrintInformation printInfo{info};
            if (util::writeToDevice(printInfo.get(), device, jobId) != true)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send printInfo");
                return false;
            }

            constexpr types::VariousModeSettings settings{};
            const commands::VariousModeSettings variousModeSettings{settings};
            if (util::writeToDevice( variousModeSettings.get(), device, jobId) != true)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send variousModeSettings");
                return false;
            }

            const auto jobData = static_cast<types::JobData*>(papplJobGetData(job));
            if (!jobData)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "jobData is null");
                return false;
            }

            const auto media = media::getMediaInfoForMedia(jobData->modelFamily, options->media.size_width, options->media.size_length, mediaType);
            if (media == media::none)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unsupported media dimensions for model family");
                return false;
            }
            const commands::AdditionalMediaInformation additionalMediaInfo{media};
            if (util::writeToDevice(additionalMediaInfo.get(), device, jobId) != true)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send additionalMediaInfo");
                return false;
            }

            const commands::SelectCompressionMode compressionModeCommand{types::CompressionMode::NoCompression};
            if (util::writeToDevice(compressionModeCommand.get(), device, jobId) != true)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send compressionModeCommand");
                return false;
            }

            return true;
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

            const auto jobId = papplJobGetID(job);
            const auto mirroredLine = util::mirrorLine(lineData);
            if (!jobData->useCompression){
                const commands::RasterGraphicsTransfer rasterGraphicsTransfer{bytesPerLine,mirroredLine};
                return util::writeToDevice(rasterGraphicsTransfer.get(), device, jobId);
            }else
            {
                const auto compressedData = util::compressLine(mirroredLine);
                const commands::RasterGraphicsTransfer rasterGraphicsTransfer{static_cast<int>(lineData.size()),compressedData};
                return util::writeToDevice(rasterGraphicsTransfer.get(), device, jobId);
            }
        }

        bool endPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, const unsigned pageNumber)
        {
            if (!job || !options || !device)
            {
                return false;
            }

            if (const auto pageCount = static_cast<unsigned>(papplJobGetImpressions(job)); pageNumber < pageCount-1 )
            {
                const commands::Print print;
                if (const auto job_id = papplJobGetID(job); util::writeToDevice(print.get(), device, job_id) != true)
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

            const commands::PrintWithFeeding printWithFeeding;
            if (util::writeToDevice(printWithFeeding.get(),device, jobId ) != true)
            {
                papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "failed to send printWithFeeding command");
                return false;
            }

            const auto jobData = static_cast<types::JobData *>(papplJobGetData(job));
            papplJobSetData(job, nullptr);
            delete jobData;

            return true;
        }

        bool getStatus([[maybe_unused]] pappl_printer_t *printer)
        {

            const auto device = papplPrinterOpenDevice(printer);
            const auto uri = papplPrinterGetDeviceURI(printer);

            if (!printerStatusPolling.contains(uri))
            {
                printerStatusPolling[uri] = false;
            }

            if (printerStatusPolling.at(uri) == false)
            {
                const commands::StatusInformationRequest cmd;

                if (util::writeToDevice(cmd.get(), device, -1 ) != true)
                {
                    return false;
                }
                types::PrinterInfo info{0};
                for (;;)
                {
                    const auto result = papplDeviceRead(device, info.raw.data(), info.raw.size());
                    if (result == -1)
                    {
                        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to read status");
                        papplPrinterCloseDevice(printer);
                        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER | PAPPL_PREASON_OFFLINE, PAPPL_PREASON_NONE);
                        return false;
                    }
                    if (info.fields().printHeadMark == 0x80) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_NONE);
                const auto fields = info.fields();
                if (fields.statusType == types::StatusType::ErrorOccurred)
                {
                    unsigned int status = 0;
                    if (fields.errorInformation2.coverOpen) status |= PAPPL_PREASON_COVER_OPEN;
                    if (fields.errorInformation1.noMedia || fields.errorInformation1.endOfMedia ||fields.errorInformation2.replacingMedia)
                        status |= PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_MEDIA_NEEDED;
                    if (fields.errorInformation1.printerInUse) status |= PAPPL_PREASON_OTHER;
                    if (fields.errorInformation2.mediaCannotBeFed) status |= PAPPL_PREASON_MEDIA_JAM;
                    if (fields.errorInformation2.systemError) status |= PAPPL_PREASON_OTHER;
                    if (fields.errorInformation2.communicationError) status |= PAPPL_PREASON_OTHER;
                    papplPrinterSetReasons(printer, status, PAPPL_PREASON_NONE);
                }
                else if (fields.statusType == types::StatusType::TurnedOff)
                {
                    papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE, PAPPL_PREASON_NONE);
                }
                else
                {
                    papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, 0xFFFF);
                }
                papplPrinterCloseDevice(printer);
                printerStatus[uri] = info;

                return true;
                //return query_and_update_status(printer);
            }else
            {
                return true;
            }
            return true;
        }
    }

    namespace media
    {
        types::MediaInfo getMediaInfoForMedia(const types::ModelFamily family, const int width, const int length, const types::MediaType type ) noexcept
        {
            //TODO: use media information from the printer instead of static data
            //TODO: simplify
            switch (type)
            {
            case types::MediaType::ContinuousLengthTape:
                {
                    if (width <= 5700)
                    {
                        switch (family)
                        {
                        case types::ModelFamily::Td2x2x:
                            return td2x2x::_57mm;
                        case types::ModelFamily::Td2x3x:
                            return td2x3x::_57mm;
                        default:
                            return td2x2x::_57mm;
                        }
                    }else
                    {
                        switch (family)
                        {
                        case types::ModelFamily::Td2x2x:
                            return td2x2x::_58mm;
                        case types::ModelFamily::Td2x3x:
                            return td2x3x::_58mm;
                        default:
                            return td2x2x::_58mm;
                        }
                    }
                }
            case types::MediaType::DieCutLabels:
                {
                    if (width <= 4000)
                    {
                        if (length <= 4000){
                            switch (family)
                            {
                            case types::ModelFamily::Td2x2x:
                                return td2x2x::_40x40mm;
                            case types::ModelFamily::Td2x3x:
                                return td2x3x::_40x40mm;
                            default:
                                return td2x2x::_40x40mm;
                            }
                        } else if (length <= 5000){
                            switch (family)
                            {
                            case types::ModelFamily::Td2x2x:
                                return td2x2x::_40x50mm;
                            case types::ModelFamily::Td2x3x:
                                return td2x3x::_40x50mm;
                            default:
                                return td2x2x::_40x50mm;
                            }
                        } else if (length <= 6000){
                            switch (family)
                            {
                            case types::ModelFamily::Td2x2x:
                                return td2x2x::_40x60mm;
                            case types::ModelFamily::Td2x3x:
                                return td2x3x::_40x60mm;
                            default:
                                return td2x2x::_40x60mm;
                            }
                        }
                    } else if (width <= 5000){
                        switch (family)
                        {
                        case types::ModelFamily::Td2x2x:
                            return td2x2x::_50x30mm;
                        case types::ModelFamily::Td2x3x:
                            return td2x3x::_50x30mm;
                        default:
                            return td2x2x::_50x30mm;
                        }
                    } else if (width <= 5100){
                        switch (family)
                        {
                        case types::ModelFamily::Td2x2x:
                            return td2x2x::_51x26mm;
                        case types::ModelFamily::Td2x3x:
                            return td2x3x::_51x26mm;
                        default:
                            return td2x2x::_51x26mm;
                        }
                    } else if (width <= 6000){
                        switch (family)
                        {
                        case types::ModelFamily::Td2x2x:
                            return td2x2x::_60x60mm;
                        case types::ModelFamily::Td2x3x:
                            return td2x3x::_60x60mm;
                        default:
                            return td2x2x::_60x60mm;
                        }
                    }
                    // Fallthrough for DieCutLabels with dimensions outside handled ranges
                    return td2x2x::_58mm;
                }
            case types::MediaType::NoMedia:
                return none;
            default:
                // Unknown media type
                return td2x2x::_58mm;
            }
            return td2x2x::_58mm ;
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

        std::vector<const char *> media{defaultMedia.begin(),defaultMedia.end()};

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
        std::ranges::copy(mediaTypes,driverData->type);

        driverData->num_media = static_cast<int>(media.size());
        std::ranges::copy(media, driverData->media);

        if (driverData->num_media > 0)
        {
            constexpr std::string_view readyMedia = "roll_current_58x0m";
            papplCopyString(driverData->media_ready[0].size_name, readyMedia.data() , readyMedia.size());
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

            }else
            {
                return false;
            }

            papplCopyString(driverData->media_default.size_name, defaultMedia[0] , std::string_view(defaultMedia[0]).size() + 1);
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
            }else
            {
                return false;
            }
        }

        // papplSystemAddTimerCallback(system, std::time({}),kStatusPollSeconds, BrotherThermal::status_timer_cb, self);

        return true;
    }
}