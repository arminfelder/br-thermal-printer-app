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

//
// Created by armin on 14.02.26.
//

#include <catch2/catch_test_macros.hpp>
#include <iostream>

#include "td2000.h"

using namespace drivers::td2000;

TEST_CASE("SwitchDynamicCommandMode::get() returns correct command", "[SwitchDynamicCommandMode]") {

    SECTION("Returns correct size for ESCP mode") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::ESCP);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
    }

    SECTION("Returns correct size for Raster mode") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::Raster);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
    }

    SECTION("Returns correct size for PTouch mode") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::PTouch);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
    }

    SECTION("Contains correct command header") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::ESCP);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[0] == 0x1B);
        REQUIRE(result[1] == 0x69);
        REQUIRE(result[2] == 0x61);
        REQUIRE(result[3] == 0x00);
    }

    SECTION("Sets correct mode byte for ESCP") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::ESCP);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == static_cast<uint8_t>(types::CommandMode::ESCP));
        REQUIRE(result[3] == 0x00);
    }

    SECTION("Sets correct mode byte for Raster") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::Raster);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == static_cast<uint8_t>(types::CommandMode::Raster));
        REQUIRE(result[3] == 0x01);
    }

    SECTION("Sets correct mode byte for PTouch") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::PTouch);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == static_cast<uint8_t>(types::CommandMode::PTouch));
        REQUIRE(result[3] == 0x03);
    }

    SECTION("Returns complete command for Raster mode") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::Raster);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x01};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::PTouch);

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }
}

TEST_CASE("SwitchDynamicCommandMode produces correct byte sequences", "[SwitchDynamicCommandMode]") {

    SECTION("ESCP mode command") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::ESCP);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x00};
        REQUIRE(result == expected);
    }

    SECTION("Raster mode command") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::Raster);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x01};
        REQUIRE(result == expected);
    }

    SECTION("PTouch mode command") {
        commands::SwitchDynamicCommandMode cmd(types::CommandMode::PTouch);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x03};
        REQUIRE(result == expected);
    }
}


TEST_CASE("VariousModeSettings::get() returns correct command", "[VariousModeSettings]") {

    SECTION("Returns correct size with default settings") {
        types::VariousModeSettings settings{};
        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);  // 3 bytes header + 1 byte settings
    }

    SECTION("Contains correct command header") {
        types::VariousModeSettings settings{};
        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() >= 3);
        REQUIRE(result[0] == 0x1B);
        REQUIRE(result[1] == 0x69);
        REQUIRE(result[2] == 0x4D);
    }

    SECTION("Appends settings byte correctly with all flags false") {
        types::VariousModeSettings settings{};
        settings.raw[0] = 0x00;

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == 0x00);
    }

    SECTION("Appends settings byte correctly with rotate180 flag set") {
        types::VariousModeSettings settings{};
        types::VariousModeSettingsFields fields{};
        fields.rotate180 = true;
        fields.peeler = false;
        settings.set_fields(fields);

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == 0x08);  // bit 3 set
    }

    SECTION("Appends settings byte correctly with peeler flag set") {
        types::VariousModeSettings settings{};
        types::VariousModeSettingsFields fields{};
        fields.rotate180 = false;
        fields.peeler = true;
        settings.set_fields(fields);

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == 0x10);  // bit 4 set
    }

    SECTION("Appends settings byte correctly with both flags set") {
        types::VariousModeSettings settings{};
        types::VariousModeSettingsFields fields{};
        fields.rotate180 = true;
        fields.peeler = true;
        settings.set_fields(fields);

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result.size() == 4);
        REQUIRE(result[3] == 0x18);  // bits 3 and 4 set
    }

    SECTION("Returns complete command sequence") {
        types::VariousModeSettings settings{};
        settings.raw[0] = 0x42;

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x4D, 0x42};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        types::VariousModeSettings settings{};
        settings.raw[0] = 0xFF;

        commands::VariousModeSettings cmd(settings);
        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }

    SECTION("Preserves all bits in settings byte") {
        types::VariousModeSettings settings{};
        settings.raw[0] = 0xAA;  // 10101010 pattern

        commands::VariousModeSettings cmd(settings);
        auto result = cmd.get();

        REQUIRE(result[3] == 0xAA);
    }
}

TEST_CASE("PrintInformation::get() returns correct command", "[PrintInformation]") {

    SECTION("Returns correct size with default PrintInfo") {
        types::PrintInfo info{};
        commands::PrintInformation cmd(info);
        auto result = cmd.get();

        REQUIRE(result.size() == 3);
    }

    SECTION("Contains correct command header") {
        types::PrintInfo info{};
        commands::PrintInformation cmd(info);
        auto result = cmd.get();

        REQUIRE(result[0] == 0x1B);
        REQUIRE(result[1] == 0x69);
        REQUIRE(result[2] == 0x7A);
    }

    SECTION("Returns complete command sequence") {
        types::PrintInfo info{};
        commands::PrintInformation cmd(info);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x7A};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        types::PrintInfo info{};
        commands::PrintInformation cmd(info);

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }

    SECTION("Object can be constructed and destructed without issues") {
        types::PrintInfo info{};
        info.mediaType = static_cast<uint8_t>(types::MediaType::DieCutLabels);
        info.mediaWith = 58;
        info.mediaLength = 100;

        {
            commands::PrintInformation cmd(info);
            auto result = cmd.get();
            REQUIRE_FALSE(result.empty());
        }
        // Destructor called here - test passes if no crash/leak
        REQUIRE(true);
    }

    SECTION("Works with different PrintInfo configurations") {
        types::PrintInfo info{};
        info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) |
                           static_cast<uint8_t>(types::PrintInfoFlags::pi_width);
        info.mediaType = static_cast<uint8_t>(types::MediaType::ContinuousLengthTape);
        info.mediaWith = 57;

        commands::PrintInformation cmd(info);
        auto result = cmd.get();

        REQUIRE(result.size() == 3);
    }
}

TEST_CASE("SpecifyMarginAmount::get() returns correct command", "[SpecifyMarginAmount]") {

    SECTION("Returns correct size") {
        commands::SpecifyMarginAmount cmd(0, 0);
        auto result = cmd.get();

        REQUIRE(result.size() == 5);  // 3 bytes header + 2 bytes (before + after)
    }

    SECTION("Contains correct command header") {
        commands::SpecifyMarginAmount cmd(0, 0);
        auto result = cmd.get();

        REQUIRE(result[0] == 0x1B);
        REQUIRE(result[1] == 0x69);
        REQUIRE(result[2] == 0x64);
    }

    SECTION("Appends before and after margins correctly with zero values") {
        commands::SpecifyMarginAmount cmd(0, 0);
        auto result = cmd.get();

        REQUIRE(result[3] == 0x00);
        REQUIRE(result[4] == 0x00);
    }

    SECTION("Appends before margin correctly") {
        commands::SpecifyMarginAmount cmd(10, 0);
        auto result = cmd.get();

        REQUIRE(result[3] == 0x0A);
        REQUIRE(result[4] == 0x00);
    }

    SECTION("Appends after margin correctly") {
        commands::SpecifyMarginAmount cmd(0, 20);
        auto result = cmd.get();

        REQUIRE(result[3] == 0x00);
        REQUIRE(result[4] == 0x14);
    }

    SECTION("Appends both margins correctly") {
        commands::SpecifyMarginAmount cmd(15, 25);
        auto result = cmd.get();

        REQUIRE(result[3] == 0x0F);
        REQUIRE(result[4] == 0x19);
    }

    SECTION("Returns complete command sequence") {
        commands::SpecifyMarginAmount cmd(5, 10);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x1B, 0x69, 0x64, 0x05, 0x0A};
        REQUIRE(result == expected);
    }

    SECTION("Handles maximum uint8_t values") {
        commands::SpecifyMarginAmount cmd(255, 255);
        auto result = cmd.get();

        REQUIRE(result[3] == 0xFF);
        REQUIRE(result[4] == 0xFF);
    }

    SECTION("Multiple calls return same result") {
        commands::SpecifyMarginAmount cmd(30, 40);

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }
}

TEST_CASE("SelectCompressionMode::get() returns correct command", "[SelectCompressionMode]") {

    SECTION("Returns correct size for NoCompression mode") {
        commands::SelectCompressionMode cmd(types::CompressionMode::NoCompression);
        auto result = cmd.get();

        REQUIRE(result.size() == 2);  // 1 byte header + 1 byte mode
    }

    SECTION("Returns correct size for Compression mode") {
        commands::SelectCompressionMode cmd(types::CompressionMode::Compression);
        auto result = cmd.get();

        REQUIRE(result.size() == 2);
    }

    SECTION("Contains correct command header") {
        commands::SelectCompressionMode cmd(types::CompressionMode::NoCompression);
        auto result = cmd.get();

        REQUIRE(result.size() >= 1);
        REQUIRE(result[0] == 0x4D);
    }

    SECTION("Sets correct mode byte for NoCompression") {
        commands::SelectCompressionMode cmd(types::CompressionMode::NoCompression);
        auto result = cmd.get();

        REQUIRE(result.size() == 2);
        REQUIRE(result[1] == static_cast<uint8_t>(types::CompressionMode::NoCompression));
        REQUIRE(result[1] == 0x00);
    }

    SECTION("Sets correct mode byte for Compression") {
        commands::SelectCompressionMode cmd(types::CompressionMode::Compression);
        auto result = cmd.get();

        REQUIRE(result.size() == 2);
        REQUIRE(result[1] == static_cast<uint8_t>(types::CompressionMode::Compression));
        REQUIRE(result[1] == 0x02);
    }

    SECTION("Returns complete command for NoCompression mode") {
        commands::SelectCompressionMode cmd(types::CompressionMode::NoCompression);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x4D, 0x00};
        REQUIRE(result == expected);
    }

    SECTION("Returns complete command for Compression mode") {
        commands::SelectCompressionMode cmd(types::CompressionMode::Compression);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x4D, 0x02};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        commands::SelectCompressionMode cmd(types::CompressionMode::Compression);

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }
}

TEST_CASE("RasterGraphicsTransfer::get() returns correct command", "[RasterGraphicsTransfer]") {

    SECTION("Returns correct size with empty raster data") {
        std::vector<uint8_t> rasterData;
        commands::RasterGraphicsTransfer cmd(0, rasterData);
        auto result = cmd.get();

        REQUIRE(result.size() == 3);  // 1 byte header + 2 bytes (0 + numberOfBytes)
    }

    SECTION("Returns correct size with raster data") {
        std::vector<uint8_t> rasterData = {0xAA, 0xBB, 0xCC, 0xDD};
        commands::RasterGraphicsTransfer cmd(4, rasterData);
        auto result = cmd.get();

        REQUIRE(result.size() == 7);  // 1 byte header + 2 bytes + 4 bytes data
    }

    SECTION("Contains correct command header") {
        std::vector<uint8_t> rasterData = {0x01, 0x02};
        commands::RasterGraphicsTransfer cmd(2, rasterData);
        auto result = cmd.get();

        REQUIRE(result.size() >= 1);
        REQUIRE(result[0] == 0x67);
    }

    SECTION("Second byte is always zero") {
        std::vector<uint8_t> rasterData = {0xFF, 0xFF, 0xFF};
        commands::RasterGraphicsTransfer cmd(3, rasterData);
        auto result = cmd.get();

        REQUIRE(result[1] == 0x00);
    }

    SECTION("Third byte contains numberOfBytes") {
        std::vector<uint8_t> rasterData = {0x01, 0x02, 0x03, 0x04, 0x05};
        commands::RasterGraphicsTransfer cmd(5, rasterData);
        auto result = cmd.get();

        REQUIRE(result[2] == 0x05);
    }

    SECTION("Appends raster data correctly") {
        std::vector<uint8_t> rasterData = {0xDE, 0xAD, 0xBE, 0xEF};
        commands::RasterGraphicsTransfer cmd(4, rasterData);
        auto result = cmd.get();

        REQUIRE(result[3] == 0xDE);
        REQUIRE(result[4] == 0xAD);
        REQUIRE(result[5] == 0xBE);
        REQUIRE(result[6] == 0xEF);
    }

    SECTION("Returns complete command sequence") {
        std::vector<uint8_t> rasterData = {0x11, 0x22, 0x33};
        commands::RasterGraphicsTransfer cmd(3, rasterData);
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x67, 0x00, 0x03, 0x11, 0x22, 0x33};
        REQUIRE(result == expected);
    }

    SECTION("Handles maximum uint8_t numberOfBytes") {
        std::vector<uint8_t> rasterData(255, 0xAA);
        commands::RasterGraphicsTransfer cmd(255, rasterData);
        auto result = cmd.get();

        REQUIRE(result[2] == 0xFF);
        REQUIRE(result.size() == 258);  // 1 + 2 + 255
    }

    SECTION("Handles typical line width for Td2x2x (56 bytes)") {
        std::vector<uint8_t> rasterData(56, 0xFF);
        commands::RasterGraphicsTransfer cmd(56, rasterData);
        auto result = cmd.get();

        REQUIRE(result[2] == 56);
        REQUIRE(result.size() == 59);  // 1 + 2 + 56
    }

    SECTION("Handles typical line width for Td2x3x (84 bytes)") {
        std::vector<uint8_t> rasterData(84, 0xFF);
        commands::RasterGraphicsTransfer cmd(84, rasterData);
        auto result = cmd.get();

        REQUIRE(result[2] == 84);
        REQUIRE(result.size() == 87);  // 1 + 2 + 84
    }

    SECTION("Multiple calls return same result") {
        std::vector<uint8_t> rasterData = {0x12, 0x34, 0x56};
        commands::RasterGraphicsTransfer cmd(3, rasterData);

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }

    SECTION("Preserves all bits in raster data") {
        std::vector<uint8_t> rasterData = {0x00, 0xFF, 0xAA, 0x55};
        commands::RasterGraphicsTransfer cmd(4, rasterData);
        auto result = cmd.get();

        REQUIRE(result[3] == 0x00);
        REQUIRE(result[4] == 0xFF);
        REQUIRE(result[5] == 0xAA);
        REQUIRE(result[6] == 0x55);
    }
}

TEST_CASE("ZeroRasterGraphics::get() returns correct command", "[ZeroRasterGraphics]") {

    SECTION("Returns correct size") {
        commands::ZeroRasterGraphics cmd;
        auto result = cmd.get();

        REQUIRE(result.size() == 1);
    }

    SECTION("Contains correct command byte") {
        commands::ZeroRasterGraphics cmd;
        auto result = cmd.get();

        REQUIRE(result[0] == 0x5A);
    }

    SECTION("Returns complete command sequence") {
        commands::ZeroRasterGraphics cmd;
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x5A};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        commands::ZeroRasterGraphics cmd;

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }

    SECTION("Default constructed object works correctly") {
        commands::ZeroRasterGraphics cmd{};
        auto result = cmd.get();

        REQUIRE_FALSE(result.empty());
        REQUIRE(result[0] == 0x5A);
    }

    SECTION("Object can be constructed and destructed without issues") {
        {
            commands::ZeroRasterGraphics cmd;
            auto result = cmd.get();
            REQUIRE_FALSE(result.empty());
        }
        // Destructor called here - test passes if no crash/leak
        REQUIRE(true);
    }
}

TEST_CASE("Print::get() returns correct command", "[Print]") {

    SECTION("Returns correct size") {
        commands::Print cmd;
        auto result = cmd.get();

        REQUIRE(result.size() == 1);
    }

    SECTION("Contains correct command byte") {
        commands::Print cmd;
        auto result = cmd.get();

        REQUIRE(result[0] == 0x0C);
    }

    SECTION("Returns complete command sequence") {
        commands::Print cmd;
        auto result = cmd.get();

        std::vector<uint8_t> expected = {0x0C};
        REQUIRE(result == expected);
    }

    SECTION("Multiple calls return same result") {
        commands::Print cmd;

        auto result1 = cmd.get();
        auto result2 = cmd.get();

        REQUIRE(result1 == result2);
    }

    SECTION("Default constructed object works correctly") {
        commands::Print cmd{};
        auto result = cmd.get();

        REQUIRE_FALSE(result.empty());
        REQUIRE(result[0] == 0x0C);
    }

    SECTION("Object can be constructed and destructed without issues") {
        {
            commands::Print cmd;
            auto result = cmd.get();
            REQUIRE_FALSE(result.empty());
        }
        // Destructor called here - test passes if no crash/leak
        REQUIRE(true);
    }
}