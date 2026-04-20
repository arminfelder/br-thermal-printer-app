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

