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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <pappl/base.h>
#include <pappl/device.h>
#include <span>
#include <stdlib.h>
#include <string>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fake_device.h"
#include "raster_pipeline.h"
#include "util.h"


using namespace util;

TEST_CASE("TIFF PackBits" )
{
    const std::vector<uint8_t> data{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x22, 0x23, 0xBA, 0xBF, 0xA2, 0x22, 0x2B
    };
    const auto result = compressLine(data);

    const std::vector<uint8_t> expected{
        0xED, 0x00, 0xFF, 0x22, 0x05, 0x23, 0xBA, 0xBF, 0xA2, 0x22, 0x2B
    };

    REQUIRE(result == expected);
}

TEST_CASE("TIFF PackBits - output longer than the line becomes one literal run", "[compressLine]")
{
    // PT spec p.35, TD spec p.31: more than 16 bytes of compressed data for a 16-byte
    // line is sent as all different data, thus 17 bytes with the length byte.
    // Pairs and single bytes alternate, thus every 3 input bytes give 4 output bytes.
    std::vector<uint8_t> data;
    for (uint8_t i = 0; data.size() < 16; ++i)
    {
        data.push_back(i);
        data.push_back(i);
        data.push_back(static_cast<uint8_t>(0x80U | i));
    }
    data.resize(16);

    const auto result = compressLine(data);

    REQUIRE(result.size() == 17);
    REQUIRE(result[0] == 15);
    REQUIRE(std::ranges::equal(std::span(result).subspan(1), data));
}

TEST_CASE("TIFF PackBits - 56-byte TD line that grows is sent as 57 bytes", "[compressLine]")
{
    std::vector<uint8_t> data;
    for (uint8_t i = 0; data.size() < 56; ++i)
    {
        data.push_back(i);
        data.push_back(i);
        data.push_back(static_cast<uint8_t>(0x80U | i));
    }
    data.resize(56);

    const auto result = compressLine(data);

    REQUIRE(result.size() == 57);
    REQUIRE(result[0] == 55);
}
TEST_CASE("reverseBitOrder - boundary values", "[reverseBitOrder]")
{
    SECTION("zero remains zero")
    {
        REQUIRE(reverseBitOrder(0x00) == 0x00);
    }

    SECTION("all ones remains all ones")
    {
        REQUIRE(reverseBitOrder(0xFF) == 0xFF);
    }
}

TEST_CASE("reverseBitOrder - single bit positions", "[reverseBitOrder]")
{
    SECTION("LSB becomes MSB")
    {
        REQUIRE(reverseBitOrder(0x01) == 0x80);  // 00000001 -> 10000000
    }

    SECTION("MSB becomes LSB")
    {
        REQUIRE(reverseBitOrder(0x80) == 0x01);  // 10000000 -> 00000001
    }
}

TEST_CASE("reverseBitOrder - alternating bits", "[reverseBitOrder]")
{
    REQUIRE(reverseBitOrder(0xAA) == 0x55);  // 10101010 -> 01010101
    REQUIRE(reverseBitOrder(0x55) == 0xAA);  // 01010101 -> 10101010
}

TEST_CASE("reverseBitOrder - symmetric patterns remain unchanged", "[reverseBitOrder]")
{
    REQUIRE(reverseBitOrder(0x18) == 0x18);  // 00011000 -> 00011000
    REQUIRE(reverseBitOrder(0x81) == 0x81);  // 10000001 -> 10000001
}

TEST_CASE("reverseBitOrder - asymmetric patterns", "[reverseBitOrder]")
{
    REQUIRE(reverseBitOrder(0x0F) == 0xF0);  // 00001111 -> 11110000
    REQUIRE(reverseBitOrder(0xF0) == 0x0F);  // 11110000 -> 00001111
    REQUIRE(reverseBitOrder(0xC0) == 0x03);  // 11000000 -> 00000011
    REQUIRE(reverseBitOrder(0x03) == 0xC0);  // 00000011 -> 11000000
}

TEST_CASE("reverseBitOrder - double reverse is identity", "[reverseBitOrder]")
{
    for (uint16_t i = 0; i <= 0xFF; ++i)
    {
        const auto byte = static_cast<uint8_t>(i);
        CAPTURE(byte);  // Will show value on failure
        REQUIRE(reverseBitOrder(reverseBitOrder(byte)) == byte);
    }
}

TEST_CASE("mirrorLine - empty input", "[mirrorLine]")
{
    const std::vector<uint8_t> data{};
    const auto result = mirrorLine(data);
    REQUIRE(result.empty());
}

TEST_CASE("mirrorLine - single byte", "[mirrorLine]")
{
    SECTION("single byte is bit-reversed")
    {
        const std::vector<uint8_t> data{0x01};
        const auto result = mirrorLine(data);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == 0x80);  // 00000001 -> 10000000
    }

    SECTION("symmetric byte stays same")
    {
        const std::vector<uint8_t> data{0xFF};
        const auto result = mirrorLine(data);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == 0xFF);
    }
}

TEST_CASE("mirrorLine - multiple bytes are reversed and bit-flipped", "[mirrorLine]")
{
    // Input: [0x01, 0x02, 0x03]
    // Step 1 - reverse order: [0x03, 0x02, 0x01]
    // Step 2 - reverse bits of each: [0xC0, 0x40, 0x80]
    const std::vector<uint8_t> data{0x01, 0x02, 0x03};
    const auto result = mirrorLine(data);

    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == reverseBitOrder(0x03));  // 0xC0
    REQUIRE(result[1] == reverseBitOrder(0x02));  // 0x40
    REQUIRE(result[2] == reverseBitOrder(0x01));  // 0x80
}

TEST_CASE("mirrorLine - double mirror is identity", "[mirrorLine]")
{
    const std::vector<uint8_t> data{0xDE, 0xAD, 0xBE, 0xEF};
    const auto once = mirrorLine(data);
    const auto twice = mirrorLine(once);

    REQUIRE(twice == data);
}

TEST_CASE("mirrorLine - alternating pattern", "[mirrorLine]")
{
    const std::vector<uint8_t> data{0xAA, 0x55};
    const auto result = mirrorLine(data);

    // Reverse order: [0x55, 0xAA]
    // Reverse bits: [0xAA, 0x55]
    REQUIRE(result.size() == 2);

    REQUIRE(result[0] == 0xAA);
    REQUIRE(result[1] == 0x55);
}

TEST_CASE("writeToDevice - null device returns false", "[writeToDevice]")
{
    const std::vector<uint8_t> data{0x01, 0x02, 0x03};

    REQUIRE(writeToDevice(data, nullptr, 1) == false);
}

TEST_CASE("debugDumpPath - uses STATE_DIRECTORY from systemd", "[debugDumpPath]")
{
    ::setenv("STATE_DIRECTORY", "/var/lib/br-thermal", 1);
    REQUIRE(debugDumpPath(5) == "/var/lib/br-thermal/br-thermal-debug-5.dump");
    ::unsetenv("STATE_DIRECTORY");
}

TEST_CASE("debugDumpPath - takes the first entry of a STATE_DIRECTORY list", "[debugDumpPath]")
{
    ::setenv("STATE_DIRECTORY", "/var/lib/a:/var/lib/b", 1);
    REQUIRE(debugDumpPath(7) == "/var/lib/a/br-thermal-debug-7.dump");
    ::unsetenv("STATE_DIRECTORY");
}

TEST_CASE("debugDumpPath - falls back to the PAPPL temporary directory", "[debugDumpPath]")
{
    ::unsetenv("STATE_DIRECTORY");
    REQUIRE(debugDumpPath(3) == std::string(papplGetTempDir()) + "/br-thermal-debug-3.dump");

    ::setenv("STATE_DIRECTORY", "", 1);
    REQUIRE(debugDumpPath(3) == std::string(papplGetTempDir()) + "/br-thermal-debug-3.dump");
    ::unsetenv("STATE_DIRECTORY");
}

TEST_CASE("writeToDevice - leaves the device open", "[writeToDevice]")
{
    test::FakeDeviceScript script{};
    pappl_device_t* device = test::openFakeDevice(script);
    REQUIRE(device != nullptr);

    const std::vector<uint8_t> data{0x1B, 0x40};
    REQUIRE(writeToDevice(data, device, 1));
    // A second write proves the handle is still usable: PAPPL owns it and closes
    // it after rendjob_cb, thus a close here would be a double free.
    REQUIRE(writeToDevice(data, device, 1));

    papplDeviceClose(device);
}

// ---------------------------------------------------------------------------
// buildHeadLine
//
// Two properties are under test.
//
// 1. No out-of-bounds read. PAPPL sizes the raster buffer from
//    header.cupsBytesPerLine, which follows the media geometry. A 12 mm tape at
//    180 dpi is 85 dots = 11 bytes against a 16-byte head. The backing vectors are
//    exact-sized on purpose, thus their heap allocation carries a redzone and a read
//    past `delivered` traps. _GLIBCXX_ASSERTIONS also traps such a read directly.
//
// 2. Correct position. The media is centred below the print head, thus the blank
//    margins must be equal on the two sides. Raster Command Reference 2.3.5 gives
//    the pin counts that the cases below use.
// ---------------------------------------------------------------------------
namespace {

// An exact-sized stand-in for the raster line PAPPL hands to rwriteline_cb.
std::vector<uint8_t> rasterLine(const size_t bytes, const uint8_t first = 1)
{
    std::vector<uint8_t> line(bytes);
    for (size_t i = 0; i < bytes; ++i)
    {
        line[i] = static_cast<uint8_t>(first + i);
    }
    return line;
}

struct Placement { size_t left; size_t width; size_t right; };

// Finds the first and the last set dot of a head line.
Placement placementOf(const std::vector<uint8_t> &head)
{
    const auto totalDots = head.size() * 8;
    size_t first = totalDots;
    size_t last = 0;
    bool any = false;
    for (size_t i = 0; i < totalDots; ++i)
    {
        if (head[i / 8] & static_cast<uint8_t>(0x80U >> (i % 8)))
        {
            if (!any) { first = i; any = true; }
            last = i;
        }
    }
    if (!any) { return {totalDots, 0, 0}; }
    return {first, last - first + 1, totalDots - last - 1};
}

} // namespace

TEST_CASE("buildHeadLine - media narrower than the print head", "[buildHeadLine]")
{
    constexpr size_t headDots = 16 * 8;

    SECTION("12 mm TZe tape: 11 delivered bytes against a 16-byte head")
    {
        constexpr size_t delivered = 11;
        const auto raster = rasterLine(delivered);

        const auto line = buildHeadLine(raster, delivered * 8, headDots);

        REQUIRE(line.size() == headDots / 8);
    }

    SECTION("4 mm minimum advertised media: 4 delivered bytes")
    {
        constexpr size_t delivered = 4;
        const std::vector<uint8_t> raster(delivered, 0xFF);

        const auto line = buildHeadLine(raster, delivered * 8, headDots);

        REQUIRE(line.size() == headDots / 8);
        // 32 set dots, and the blank dots split evenly.
        const auto place = placementOf(line);
        REQUIRE(place.width == delivered * 8);
        REQUIRE(place.left == place.right);
    }

    SECTION("zero-length raster yields a fully blank head line")
    {
        const auto line = buildHeadLine(std::span<const uint8_t>{}, 0, headDots);

        REQUIRE(line.size() == headDots / 8);
        REQUIRE(std::ranges::all_of(line, [](const uint8_t b) { return b == 0; }));
    }

    SECTION("a short cupsWidth never reads past the buffer")
    {
        // deliveredDots claims more than the buffer holds; the clamp must win.
        const std::vector<uint8_t> raster(3, 0xFF);
        const auto line = buildHeadLine(raster, 1000, headDots);
        REQUIRE(line.size() == headDots / 8);
        REQUIRE(placementOf(line).width == 3 * 8);
    }
}

TEST_CASE("buildHeadLine - media at or wider than the print head", "[buildHeadLine]")
{
    constexpr size_t headDots = 16 * 8;

    SECTION("exact fit behaves identically to a plain mirror")
    {
        const auto raster = rasterLine(headDots / 8, 0xA0);
        REQUIRE(buildHeadLine(raster, headDots, headDots) == mirrorLine(raster));
    }

    SECTION("wider media is cropped evenly on both sides")
    {
        constexpr size_t delivered = 22;
        const std::vector<uint8_t> raster(delivered, 0xFF);

        const auto line = buildHeadLine(raster, delivered * 8, headDots);

        REQUIRE(line.size() == headDots / 8);
        // Every head dot is covered, thus nothing is blank.
        REQUIRE(std::ranges::all_of(line, [](const uint8_t b) { return b == 0xFF; }));
    }
}

TEST_CASE("buildHeadLine - margins match Raster Command Reference 2.3.5", "[buildHeadLine]")
{
    // Label width in dots, and the blank pins each side of it, for a 448-pin head.
    // The label carries its own print-area margin, thus these are the label edges,
    // not the print area of the table.
    struct Row { const char *name; size_t labelDots; size_t blankEachSide; };
    constexpr size_t headDots = 448;

    const std::array rows{
        Row{"30 x 30 mm", 240, 104},
        Row{"40 x 40 mm", 320,  64},
        Row{"50 x 30 mm", 400,  24},
        Row{"51 x 26 mm", 406,  21},
    };

    for (const auto &row : rows)
    {
        const std::vector<uint8_t> raster((row.labelDots + 7) / 8, 0xFF);
        const auto line = buildHeadLine(raster, row.labelDots, headDots);
        const auto place = placementOf(line);

        INFO(row.name);
        REQUIRE(line.size() == headDots / 8);
        REQUIRE(place.width == row.labelDots);
        REQUIRE(place.left == row.blankEachSide);
        REQUIRE(place.right == row.blankEachSide);
    }

    SECTION("672-pin head at 300 dpi")
    {
        constexpr size_t wideHead = 672;
        const std::array wideRows{
            Row{"30 x 30 mm", 354, 159},
            Row{"40 x 40 mm", 472, 100},
        };
        for (const auto &row : wideRows)
        {
            const std::vector<uint8_t> raster((row.labelDots + 7) / 8, 0xFF);
            const auto line = buildHeadLine(raster, row.labelDots, wideHead);
            const auto place = placementOf(line);

            INFO(row.name);
            REQUIRE(line.size() == wideHead / 8);
            REQUIRE(place.width == row.labelDots);
            REQUIRE(place.left == row.blankEachSide);
            REQUIRE(place.right == row.blankEachSide);
        }
    }

    SECTION("media wider than the head covers every pin")
    {
        // 60 x 60 mm is 480 dots against a 448-pin head; spec 2.3.5 gives 0 blank pins.
        const std::vector<uint8_t> raster(60, 0xFF);
        const auto line = buildHeadLine(raster, 480, headDots);
        const auto place = placementOf(line);
        REQUIRE(place.left == 0);
        REQUIRE(place.width == headDots);
        REQUIRE(place.right == 0);
    }
}

// ---------------------------------------------------------------------------
// readStatusPacket
//
// The three outcomes must stay distinct: the drivers poll again on NoData but
// stop on Error. A second "probe" read to tell them apart would consume the
// printer's reply.
// ---------------------------------------------------------------------------
namespace
{
    struct FakeStatusBlock
    {
        std::array<uint8_t, 32> raw{};
    };
}

TEST_CASE("readStatusPacket - full block with head mark is a packet", "[readStatusPacket]")
{
    test::FakeDeviceScript script{.results = {32}};
    pappl_device_t* device = test::openFakeDevice(script);
    REQUIRE(device != nullptr);

    FakeStatusBlock block{};
    REQUIRE(readStatusPacket(device, block) == ReadResult::Packet);
    REQUIRE(block.raw.front() == 0x80);

    papplDeviceClose(device);
}

TEST_CASE("readStatusPacket - zero-length and short reads are NoData", "[readStatusPacket]")
{
    test::FakeDeviceScript script{.results = {0, 5}};
    pappl_device_t* device = test::openFakeDevice(script);
    REQUIRE(device != nullptr);

    FakeStatusBlock block{};
    REQUIRE(readStatusPacket(device, block) == ReadResult::NoData);
    REQUIRE(readStatusPacket(device, block) == ReadResult::NoData);

    papplDeviceClose(device);
}

TEST_CASE("readStatusPacket - wrong head mark is NoData", "[readStatusPacket]")
{
    test::FakeDeviceScript script{.results = {32}, .firstByte = 0x00};
    pappl_device_t* device = test::openFakeDevice(script);
    REQUIRE(device != nullptr);

    FakeStatusBlock block{};
    REQUIRE(readStatusPacket(device, block) == ReadResult::NoData);

    papplDeviceClose(device);
}

TEST_CASE("readStatusPacket - negative read is Error", "[readStatusPacket]")
{
    test::FakeDeviceScript script{.results = {-1}};
    pappl_device_t* device = test::openFakeDevice(script);
    REQUIRE(device != nullptr);

    FakeStatusBlock block{};
    REQUIRE(readStatusPacket(device, block) == ReadResult::Error);

    papplDeviceClose(device);
}

TEST_CASE("JobData::takeLine - lines above the page limit are dropped", "[pipeline]")
{
    brother::pipeline::JobData<int> data{};
    data.lineLimit = 2;
    REQUIRE(data.takeLine());
    REQUIRE(data.takeLine());
    REQUIRE_FALSE(data.takeLine());
}

TEST_CASE("JobData::takeLine - a limit of 0 means no limit", "[pipeline]")
{
    brother::pipeline::JobData<int> data{};
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE(data.takeLine());
    }
}

namespace
{
    using TestStatus = brother::raster::types::StatusBlock<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>;

    brother::pipeline::JobStartStatus checkStart(test::FakeDeviceScript& script)
    {
        auto* device = test::openFakeDevice(script);
        REQUIRE(device != nullptr);
        TestStatus info{};
        const auto status = brother::pipeline::requestStatusAtJobStart(device, -1, info);
        papplDeviceClose(device);
        return status;
    }
}

TEST_CASE("requestStatusAtJobStart - a clean reply is ready", "[pipeline]")
{
    test::FakeDeviceScript script{.results = {32}};
    REQUIRE(checkStart(script) == brother::pipeline::JobStartStatus::Ready);
}

TEST_CASE("requestStatusAtJobStart - an error reply stops the job", "[pipeline]")
{
    // Status type (offset 18) = 02h, error occurred.
    std::vector<uint8_t> packet(32, 0);
    packet[0] = 0x80;
    packet[18] = 0x02;
    test::FakeDeviceScript script{.results = {32}, .packet = packet};
    REQUIRE(checkStart(script) == brother::pipeline::JobStartStatus::PrinterError);
}

TEST_CASE("requestStatusAtJobStart - a read error is a device error", "[pipeline]")
{
    test::FakeDeviceScript script{.results = {-1}};
    REQUIRE(checkStart(script) == brother::pipeline::JobStartStatus::DeviceError);
}

TEST_CASE("requestStatusAtJobStart - no reply does not stop the job", "[pipeline]")
{
    test::FakeDeviceScript script{.results = {}};
    REQUIRE(checkStart(script) == brother::pipeline::JobStartStatus::Ready);
}

TEST_CASE("RasterTransfer - opcode and count form are independent", "[raster]")
{
    using brother::raster::commands::RasterCount;
    using brother::raster::commands::RasterTransfer;
    const std::vector<uint8_t> data{0xAA, 0xBB};

    // TD-2000, QL, RJ: 'g' 00h {n}.
    REQUIRE(RasterTransfer<0x67, RasterCount::ZeroPrefixedByte>{2, data}.get() ==
            std::vector<uint8_t>{0x67, 0x00, 0x02, 0xAA, 0xBB});
    // PT-E550W family: 'G' {n1} {n2}.
    REQUIRE(RasterTransfer<0x47, RasterCount::LittleEndian16>{300, data}.get() ==
            std::vector<uint8_t>{0x47, 0x2C, 0x01, 0xAA, 0xBB});
    // PT-H500/P700/E500 raster reference p.37: 'g' {n1} {n2}, k = n1 + n2 * 256.
    REQUIRE(RasterTransfer<0x67, RasterCount::LittleEndian16>{300, data}.get() ==
            std::vector<uint8_t>{0x67, 0x2C, 0x01, 0xAA, 0xBB});
}

namespace
{
    constexpr uint8_t kPrintingCompleted = 0x01;
    constexpr uint8_t kErrorOccurred     = 0x02;
    constexpr uint8_t kPhaseChange       = 0x06;
    constexpr uint8_t kReceiving         = 0x00;
    constexpr uint8_t kPrinting          = 0x01;

    struct DrainResult
    {
        brother::pipeline::JobEndStatus status;
        int blocks;
        size_t reads;
    };

    // No delay, 2 reads without data when idle, 5 after a "printing" block.
    DrainResult drain(test::FakeDeviceScript& script)
    {
        auto* device = test::openFakeDevice(script);
        REQUIRE(device != nullptr);
        int blocks = 0;
        const auto status = brother::pipeline::drainJobEnd<TestStatus>(
            device, [&](const TestStatus&) { ++blocks; },
            brother::pipeline::JobEndTiming{.idleReads = 2, .printingReads = 5, .delay = std::chrono::milliseconds{0}});
        papplDeviceClose(device);
        return {status, blocks, script.calls};
    }
}

TEST_CASE("drainJobEnd - printing, completed, receiving is a completed job", "[pipeline]")
{
    test::FakeDeviceScript script{};
    script.queue = {test::statusBlock(kPhaseChange, kPrinting), test::statusBlock(kPrintingCompleted),
                    test::statusBlock(kPhaseChange, kReceiving)};
    const auto result = drain(script);
    REQUIRE(result.status == brother::pipeline::JobEndStatus::Completed);
    REQUIRE(result.blocks == 3);
    REQUIRE(result.reads == 3);
}

TEST_CASE("drainJobEnd - a silent printer gets only the idle time", "[pipeline]")
{
    test::FakeDeviceScript script{};
    const auto result = drain(script);
    REQUIRE(result.status == brother::pipeline::JobEndStatus::NoReply);
    REQUIRE(result.reads == 2);
}

// ESC i S is not permitted while the printer prints, thus the wait must not stop
// after the idle time when the printer reported the printing phase.
TEST_CASE("drainJobEnd - the wait is longer after a printing block", "[pipeline]")
{
    test::FakeDeviceScript script{};
    script.queue = {test::statusBlock(kPhaseChange, kPrinting)};
    const auto result = drain(script);
    REQUIRE(result.status == brother::pipeline::JobEndStatus::Timeout);
    REQUIRE(result.reads == 6);
}

TEST_CASE("drainJobEnd - printing completed without a receiving block is a completed job", "[pipeline]")
{
    test::FakeDeviceScript script{};
    script.queue = {test::statusBlock(kPhaseChange, kPrinting), test::statusBlock(kPrintingCompleted)};
    const auto result = drain(script);
    REQUIRE(result.status == brother::pipeline::JobEndStatus::Completed);
    REQUIRE(result.reads == 4);
}

TEST_CASE("drainJobEnd - an error block is a printer error", "[pipeline]")
{
    test::FakeDeviceScript script{};
    script.queue = {test::statusBlock(kPhaseChange, kPrinting), test::statusBlock(kErrorOccurred, 0, 0x01)};
    const auto result = drain(script);
    REQUIRE(result.status == brother::pipeline::JobEndStatus::PrinterError);
    REQUIRE(result.blocks == 2);
}

TEST_CASE("drainJobEnd - a failed read is a device error", "[pipeline]")
{
    test::FakeDeviceScript script{.results = {-1}};
    REQUIRE(drain(script).status == brother::pipeline::JobEndStatus::DeviceError);
}
