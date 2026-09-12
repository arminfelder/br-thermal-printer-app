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
#include <ranges>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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
