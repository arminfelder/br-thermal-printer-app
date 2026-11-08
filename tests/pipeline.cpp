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

// The driver callbacks with PAPPL objects. The PAPPL system does not run and the
// printer is paused, thus PAPPL starts no job thread. The test calls the callbacks
// in the sequence that PAPPL uses.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <pappl/pappl.h>
}

#include "driver_registry.h"
#include "fake_device.h"
#include "util.h"

namespace
{
    constexpr uint8_t kReply             = 0x00;
    constexpr uint8_t kPrintingCompleted = 0x01;
    constexpr uint8_t kErrorOccurred     = 0x02;
    constexpr uint8_t kPhaseChange       = 0x06;
    constexpr uint8_t kReceiving         = 0x00;
    constexpr uint8_t kPrinting          = 0x01;
    constexpr uint8_t kCoverOpen         = 0x10;   // error information 2, bit 4

    const std::vector<uint8_t> kStatusRequest{0x1B, 0x69, 0x53};
    const std::vector<uint8_t> kPrintWithFeeding{0x1A};

    bool driverCallback(pappl_system_t*, const char* driverName, const char*, const char* deviceId,
                        pappl_pr_driver_data_t* data, ipp_t** attrs, void*)
    {
        const auto* entry = registry::byName(driverName);
        data->format = "image/pwg-raster";
        return entry != nullptr && entry->updateDriverData(data, util::modelFromDeviceId(deviceId), attrs);
    }

    bool endsWith(const std::vector<uint8_t>& data, const std::vector<uint8_t>& tail)
    {
        return data.size() >= tail.size() && std::equal(tail.rbegin(), tail.rend(), data.rbegin());
    }

    struct Harness
    {
        test::FakeDeviceScript script{};
        // The blocks that the fake printer sends after Control-Z.
        std::vector<std::vector<uint8_t>> jobEndReply{
            test::statusBlock(kPhaseChange, kPrinting),
            test::statusBlock(kPrintingCompleted),
            test::statusBlock(kPhaseChange, kReceiving),
        };
        std::filesystem::path   spool;
        pappl_system_t*         system  = nullptr;
        pappl_printer_t*        printer = nullptr;
        pappl_job_t*            job     = nullptr;
        pappl_pr_options_t*     options = nullptr;
        pappl_device_t*         device  = nullptr;
        pappl_pr_driver_data_t  data{};
        std::vector<pappl_pr_driver_t> driverList;
        // The reply of the fake printer to ESC i S.
        std::vector<uint8_t> statusReply = test::statusBlock(kReply);

        // vendorDefaults: the "<name>-default" values of the printer, as the "modify"
        // subcommand sets them. papplJobCreateWithFile of PAPPL 1.4.9 cannot give options
        // to a job: it encodes them as printer defaults.
        Harness(const char* driverName, const char* model,
                const std::vector<std::pair<const char*, const char*>>& vendorDefaults = {},
                const ipp_quality_t quality = IPP_QUALITY_NORMAL)
        {
            spool = std::filesystem::temp_directory_path() /
                    ("br-thermal-pipeline-test-" + std::to_string(getpid()));
            std::filesystem::create_directories(spool);

            script.onWrite = [this](const std::span<const uint8_t> written)
            {
                const std::vector<uint8_t> command(written.begin(), written.end());
                if (command == kStatusRequest)
                {
                    script.queue.push_back(statusReply);
                }
                else if (command == kPrintWithFeeding)
                {
                    script.queue.insert(script.queue.end(), jobEndReply.begin(), jobEndReply.end());
                }
            };
            test::useFakeDevice(script);

            system = papplSystemCreate(PAPPL_SOPTIONS_NONE, "br-thermal-test", 0, nullptr, spool.c_str(), "-",
                                       PAPPL_LOGLEVEL_FATAL, nullptr, false);
            REQUIRE(system != nullptr);
            for (const auto& entry : registry::drivers)
            {
                driverList.push_back({entry.name, entry.info, nullptr, nullptr});
            }
            papplSystemSetPrinterDrivers(system, static_cast<int>(driverList.size()), driverList.data(), nullptr,
                                         nullptr, driverCallback, nullptr);

            const std::string deviceId = std::string("MFG:Brother;MDL:") + model + ";";
            printer = papplPrinterCreate(system, 0, "Test Printer", driverName, deviceId.c_str(), test::fakeDeviceUri);
            REQUIRE(printer != nullptr);
            papplPrinterPause(printer);
            REQUIRE(papplPrinterGetDriverData(printer, &data) != nullptr);

            cups_option_t* vendor = nullptr;
            int numVendor = 0;
            for (const auto& [name, value] : vendorDefaults)
            {
                numVendor = cupsAddOption(name, value, numVendor, &vendor);
            }
            data.quality_default = quality;
            REQUIRE(papplPrinterSetDriverDefaults(printer, &data, numVendor, vendor));
            cupsFreeOptions(numVendor, vendor);

            const auto file = spool / "job.bin";
            std::ofstream(file) << "test";
            job = papplJobCreateWithFile(printer, "tester", "application/octet-stream", "test", 0, nullptr, file.c_str());
            REQUIRE(job != nullptr);
            options = papplJobCreatePrintOptions(job, 1, false);
            REQUIRE(options != nullptr);

            device = papplDeviceOpen(test::fakeDeviceUri, "test", nullptr, nullptr);
            REQUIRE(device != nullptr);
        }

        Harness(const Harness&) = delete;
        Harness& operator=(const Harness&) = delete;

        ~Harness()
        {
            papplDeviceClose(device);
            papplJobDeletePrintOptions(options);
            // PAPPL frees the file name of a job only when the job is in a final state.
            papplJobCancel(job);
            // papplSystemDelete does not free the printers of a system that did not run.
            papplPrinterDelete(printer);
            papplSystemDelete(system);
            test::g_script = nullptr;
            std::error_code ignored;
            std::filesystem::remove_all(spool, ignored);
        }

        [[nodiscard]] std::vector<unsigned char> blankLine() const
        {
            return std::vector<unsigned char>(options->header.cupsBytesPerLine, 0);
        }

        bool page(const unsigned number, const int lines)
        {
            if (!data.rstartpage_cb(job, options, device, number))
            {
                return false;
            }
            const auto line = blankLine();
            for (int y = 0; y < lines; ++y)
            {
                data.rwriteline_cb(job, options, device, static_cast<unsigned>(y), line.data());
            }
            return data.rendpage_cb(job, options, device, number);
        }
    };

    // TD-2020: 'g' 00h 38h and a 56-byte line.
    std::vector<uint8_t> tdBlankLines(const int count)
    {
        std::vector<uint8_t> bytes;
        for (int i = 0; i < count; ++i)
        {
            bytes.insert(bytes.end(), {0x67, 0x00, 0x38});
            bytes.insert(bytes.end(), 56, 0x00);
        }
        return bytes;
    }
}

TEST_CASE("pipeline - TD-2020 job sends the commands in the sequence of the raster spec", "[pipeline]")
{
    Harness h(drivers::td2000::driverName, "TD-2020");

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));

    // Invalidate (200 x 00h), ESC @, ESC i S, ESC i a 01h.
    std::vector<uint8_t> jobStart(200, 0x00);
    jobStart.insert(jobStart.end(), {0x1B, 0x40, 0x1B, 0x69, 0x53, 0x1B, 0x69, 0x61, 0x01});
    REQUIRE(h.script.written == jobStart);

    REQUIRE(h.page(0, 3));
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));

    auto tail = tdBlankLines(3);
    tail.push_back(0x1A);
    REQUIRE(endsWith(h.script.written, tail));
    REQUIRE(papplJobGetData(h.job) == nullptr);
}

TEST_CASE("pipeline - FF comes before the second page, not after the last page", "[pipeline]")
{
    Harness h(drivers::td2000::driverName, "TD-2020");

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    REQUIRE(h.page(0, 1));
    const auto afterFirstPage = h.script.written.size();
    REQUIRE(h.script.written.back() != 0x0C);

    REQUIRE(h.page(1, 1));
    REQUIRE(h.script.written.at(afterFirstPage) == 0x0C);
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));
    REQUIRE(h.script.written.back() == 0x1A);
}

TEST_CASE("pipeline - an error block after the last page fails the job and sets the printer reasons", "[pipeline]")
{
    Harness h(drivers::td2000::driverName, "TD-2020");
    h.jobEndReply = {test::statusBlock(kPhaseChange, kPrinting), test::statusBlock(kErrorOccurred, 0, 0, kCoverOpen)};

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    REQUIRE(h.page(0, 1));
    REQUIRE_FALSE(h.data.rendjob_cb(h.job, h.options, h.device));
    REQUIRE((papplPrinterGetReasons(h.printer) & PAPPL_PREASON_COVER_OPEN) != 0);
}

TEST_CASE("pipeline - no ESC i S during a job, ESC i S again after the job", "[pipeline]")
{
    Harness h(drivers::td2000::driverName, "TD-2020");

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    const auto duringJob = h.script.written.size();
    REQUIRE(h.data.status_cb(h.printer));
    REQUIRE(h.script.written.size() == duringJob);

    REQUIRE(h.page(0, 1));
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));
    REQUIRE(h.data.status_cb(h.printer));
    REQUIRE(endsWith(h.script.written, kStatusRequest));
}

TEST_CASE("pipeline - a failed page fails the job and the printer goes back to pull mode", "[pipeline]")
{
    Harness h(drivers::td2000::driverName, "TD-2020");

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    papplCopyString(h.options->media.type, "not-a-media-type", sizeof(h.options->media.type));
    REQUIRE_FALSE(h.page(0, 1));
    REQUIRE_FALSE(h.data.rendjob_cb(h.job, h.options, h.device));
    REQUIRE_FALSE(endsWith(h.script.written, kPrintWithFeeding));
    REQUIRE(papplJobGetData(h.job) == nullptr);

    REQUIRE(h.data.status_cb(h.printer));
    REQUIRE(endsWith(h.script.written, kStatusRequest));
}

// PT-E550W and PT-P750W do not support ESC i S (raster spec 3). The model comes from
// the driver data, thus the idle status callback must not open the device at all.
TEST_CASE("pipeline - idle status poll sends ESC i S only to models that support it", "[pipeline]")
{
    SECTION("PT-P750W: no ESC i S")
    {
        Harness h(drivers::pte550w::driverName, "PT-P750W");
        REQUIRE(h.data.status_cb(h.printer));
        REQUIRE(h.script.written.empty());
        REQUIRE(h.script.calls == 0);
    }
    SECTION("PT-P710BT: ESC i S")
    {
        Harness h(drivers::pte550w::driverName, "PT-P710BT");
        REQUIRE(h.data.status_cb(h.printer));
        REQUIRE(h.script.written == kStatusRequest);
    }
}

TEST_CASE("pipeline - PT-P750W job: 100 invalidate bytes, no ESC i S, compressed lines", "[pipeline]")
{
    Harness h(drivers::pte550w::driverName, "PT-P750W");

    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    std::vector<uint8_t> jobStart(100, 0x00);
    jobStart.insert(jobStart.end(), {0x1B, 0x40});
    REQUIRE(h.script.written == jobStart);

    REQUIRE(h.page(0, 2));
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));

    // 'G' 02h 00h + PackBits of 16 zero bytes (F1h 00h), two lines, then Control-Z.
    const std::vector<uint8_t> tail{0x47, 0x02, 0x00, 0xF1, 0x00, 0x47, 0x02, 0x00, 0xF1, 0x00, 0x1A};
    REQUIRE(endsWith(h.script.written, tail));
}

namespace
{
    // The position after the last occurrence of `command` in `data`, or npos.
    size_t after(const std::vector<uint8_t>& data, const std::vector<uint8_t>& command)
    {
        const auto found = std::ranges::find_end(data, command);
        return found.empty() ? std::string::npos
                             : static_cast<size_t>(found.end() - data.begin());
    }

    // The byte that follows ESC i {op} in the page commands.
    uint8_t parameterOf(const std::vector<uint8_t>& data, const uint8_t op)
    {
        const auto position = after(data, {0x1B, 0x69, op});
        REQUIRE(position != std::string::npos);
        return data.at(position);
    }
}

// Each print quality gives 180x180 dpi, thus ESC i K has no bit 6.
TEST_CASE("pipeline - PT-P750W default job: full cut, no chain, 2 mm margin", "[pipeline][options]")
{
    Harness h(drivers::pte550w::driverName, "PT-P750W", {}, IPP_QUALITY_HIGH);
    REQUIRE(h.options->header.HWResolution[1] == 180);
    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    REQUIRE(h.data.rstartpage_cb(h.job, h.options, h.device, 0));

    REQUIRE(parameterOf(h.script.written, 0x4D) == 0x40);
    REQUIRE(parameterOf(h.script.written, 0x4B) == 0x08);
    REQUIRE(parameterOf(h.script.written, 0x41) == 0x01);
    const auto margin = after(h.script.written, {0x1B, 0x69, 0x64});
    REQUIRE(h.script.written.at(margin) == 14);
    REQUIRE(h.script.written.at(margin + 1) == 0);
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));
}

// A CUPS queue with a PPD cannot send vendor options with the job, thus the printer
// defaults must reach the commands.
TEST_CASE("pipeline - PT-P750W printer defaults reach ESC i M, ESC i K, ESC i A and ESC i d", "[pipeline][options]")
{
    Harness h(drivers::pte550w::driverName, "PT-P750W",
              {{"brother-cut-mode", "half"}, {"brother-chain-printing", "on"}, {"brother-margin", "25"},
               {"brother-cut-every", "3"}},
              IPP_QUALITY_DRAFT);
    REQUIRE(h.options->header.HWResolution[1] == 180);
    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    REQUIRE(h.data.rstartpage_cb(h.job, h.options, h.device, 0));

    REQUIRE(parameterOf(h.script.written, 0x4D) == 0x40);
    REQUIRE(parameterOf(h.script.written, 0x4B) == 0x04);   // half cut, chain printing, normal resolution
    REQUIRE(parameterOf(h.script.written, 0x41) == 0x03);
    const auto margin = after(h.script.written, {0x1B, 0x69, 0x64});
    REQUIRE(h.script.written.at(margin) == 177);            // 25 mm at 180 dpi
    REQUIRE(h.script.written.at(margin + 1) == 0);
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));
}

TEST_CASE("pipeline - the PT-P710BT gets no ESC i A", "[pipeline][options]")
{
    Harness h(drivers::pte550w::driverName, "PT-P710BT");
    REQUIRE(h.data.rstartjob_cb(h.job, h.options, h.device));
    REQUIRE(h.data.rstartpage_cb(h.job, h.options, h.device, 0));
    REQUIRE(after(h.script.written, {0x1B, 0x69, 0x41}) == std::string::npos);
    REQUIRE(h.data.rendjob_cb(h.job, h.options, h.device));
}

// Status block offset 10 is the tape width, offset 11 the tape type (01h laminated).
TEST_CASE("pipeline - the ready media follows the tape that the printer reports", "[pipeline][media]")
{
    Harness h(drivers::pte550w::driverName, "PT-P710BT");
    REQUIRE(h.data.media_ready[0].size_width == 2400);
    REQUIRE(h.data.media_ready[0].left_margin == 297);

    h.statusReply[10] = 12;
    h.statusReply[11] = 0x01;
    REQUIRE(h.data.status_cb(h.printer));

    pappl_pr_driver_data_t now{};
    REQUIRE(papplPrinterGetDriverData(h.printer, &now) != nullptr);
    REQUIRE(now.media_ready[0].size_width == 1200);
    REQUIRE(now.media_ready[0].left_margin == 99);
    REQUIRE(now.media_ready[0].right_margin == 99);
    REQUIRE(std::string(now.media_ready[0].size_name) == "roll_current_12x100mm");
    REQUIRE(now.media_default.size_width == 1200);
    REQUIRE(now.media_default.left_margin == 99);

    SECTION("no tape and an unknown width change nothing")
    {
        h.statusReply[10] = 0;
        REQUIRE(h.data.status_cb(h.printer));
        h.statusReply[10] = 36;
        REQUIRE(h.data.status_cb(h.printer));
        REQUIRE(papplPrinterGetDriverData(h.printer, &now) != nullptr);
        REQUIRE(now.media_ready[0].size_width == 1200);
    }
}
