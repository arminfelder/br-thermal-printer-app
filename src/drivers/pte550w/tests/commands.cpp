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
#include "pte550w.h"

using namespace drivers::pte550w;

// ---------------------------------------------------------------------------
// SwitchDynamicCommandMode
// ---------------------------------------------------------------------------

TEST_CASE("SwitchDynamicCommandMode - raster mode", "[pte550w][commands]")
{
    commands::SwitchDynamicCommandMode cmd(types::CommandMode::Raster);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x01};
    REQUIRE(result == expected);
}

// ---------------------------------------------------------------------------
// SelectCompressionMode – TIFF PackBits is mandatory for this family
// ---------------------------------------------------------------------------

TEST_CASE("SelectCompressionMode - compression enabled", "[pte550w][commands]")
{
    commands::SelectCompressionMode cmd(types::CompressionMode::Compression);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x4D, 0x02};
    REQUIRE(result == expected);
}

TEST_CASE("SelectCompressionMode - no compression", "[pte550w][commands]")
{
    commands::SelectCompressionMode cmd(types::CompressionMode::NoCompression);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x4D, 0x00};
    REQUIRE(result == expected);
}

// ---------------------------------------------------------------------------
// RasterGraphicsTransfer – 16 bytes per line for this printer family
// ---------------------------------------------------------------------------

TEST_CASE("RasterGraphicsTransfer - 16-byte line", "[pte550w][commands]")
{
    const std::vector<uint8_t> rasterData(16, 0xFF);
    commands::RasterGraphicsTransfer cmd(16, rasterData);
    const auto result = cmd.get();

    // Header: 0x47 ('G'), <n1>, <n2> (little-endian byte count)
    REQUIRE(result[0] == 0x47);
    REQUIRE(result[1] == 0x10); // 16 decimal = 0x10 (n1)
    REQUIRE(result[2] == 0x00); // (n2)
    REQUIRE(result.size() == 3 + 16);
}

TEST_CASE("RasterGraphicsTransfer - 257-byte line (large buffer test)", "[pte550w][commands]")
{
    const std::vector<uint8_t> rasterData(257, 0xAA);
    commands::RasterGraphicsTransfer cmd(257, rasterData);
    const auto result = cmd.get();

    REQUIRE(result[0] == 0x47);
    REQUIRE(result[1] == 0x01); // 257 % 256 = 1
    REQUIRE(result[2] == 0x01); // 257 / 256 = 1
    REQUIRE(result.size() == 3 + 257);
}

TEST_CASE("RasterGraphicsTransfer - empty raster data", "[pte550w][commands]")
{
    const std::vector<uint8_t> empty;
    commands::RasterGraphicsTransfer cmd(0, empty);
    const auto result = cmd.get();

    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 0x47);
    REQUIRE(result[1] == 0x00);
    REQUIRE(result[2] == 0x00);
}

// ---------------------------------------------------------------------------
// StatusInformationRequest
// ---------------------------------------------------------------------------

TEST_CASE("StatusInformationRequest - correct bytes", "[pte550w][commands]")
{
    commands::StatusInformationRequest cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 0x1B);
    REQUIRE(result[1] == 0x69);
    REQUIRE(result[2] == 0x53);
}

// ---------------------------------------------------------------------------
// PrintInformation
// ---------------------------------------------------------------------------

TEST_CASE("PrintInformation - default (laminated tape, 180 dpi, quality=0)", "[pte550w][commands]")
{
    types::PrintInfoFields info{};
    info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) |
                       static_cast<uint8_t>(types::PrintInfoFlags::pi_width);
    info.mediaType   = static_cast<uint8_t>(types::MediaType::LaminatedTape);
    info.mediaWidth  = 24; // 24mm
    info.quality     = 0x00; // standard (180 dpi)

    commands::PrintInformation cmd(info);
    const auto result = cmd.get();

    REQUIRE(result.size() == 13); // ESC i z (3) + payload (10)
    REQUIRE(result[0] == 0x1B);
    REQUIRE(result[1] == 0x69);
    REQUIRE(result[2] == 0x7A);
    REQUIRE(result[3] == 0x06); // validFields (pi_kind | pi_width)
    REQUIRE(result[4] == 0x01); // mediaType (LaminatedTape)
    REQUIRE(result[5] == 0x18); // mediaWidth (24)
    REQUIRE(result[12] == 0x00); // quality (at offset 9 of payload, result[12])
}

TEST_CASE("PrintInformation - high quality (360 dpi, quality=1)", "[pte550w][commands]")
{
    types::PrintInfoFields info{};
    info.validFields = static_cast<uint8_t>(types::PrintInfoFlags::pi_kind) |
                       static_cast<uint8_t>(types::PrintInfoFlags::pi_width) |
                       static_cast<uint8_t>(types::PrintInfoFlags::pi_quality);
    info.quality = 0x01; // high quality (360 dpi)

    commands::PrintInformation cmd(info);
    const auto result = cmd.get();

    REQUIRE(result.size() == 13);
    REQUIRE(result[3] == 0x46); // kind | width | quality
    REQUIRE(result[12] == 0x01);
}

// ---------------------------------------------------------------------------
// PrintWithFeeding
// ---------------------------------------------------------------------------

TEST_CASE("PrintWithFeeding - correct byte", "[pte550w][commands]")
{
    commands::PrintWithFeeding cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x1A);
}

// ---------------------------------------------------------------------------
// Print (mid-job page separator)
// ---------------------------------------------------------------------------

TEST_CASE("Print - correct byte", "[pte550w][commands]")
{
    commands::Print cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x0C);
}

// ---------------------------------------------------------------------------
// ZeroRasterGraphics
// ---------------------------------------------------------------------------

TEST_CASE("ZeroRasterGraphics - correct byte", "[pte550w][commands]")
{
    commands::ZeroRasterGraphics cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x5A);
}

// ---------------------------------------------------------------------------
// SpecifyMarginAmount – 14 dots minimum at 180 dpi (spec §2.6)
// ---------------------------------------------------------------------------

TEST_CASE("SpecifyMarginAmount - 14 dot feed margin", "[pte550w][commands]")
{
    commands::SpecifyMarginAmount cmd(14, 0);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x1B, 0x69, 0x64, 0x0E, 0x00};
    REQUIRE(result == expected);
}

// ---------------------------------------------------------------------------
// VariousModeSettings
// ---------------------------------------------------------------------------

TEST_CASE("VariousModeSettings - autoCut enabled, mirrorPrint disabled", "[pte550w][commands]")
{
    // Spec §4 p.33: bit 6=auto cut, bit 7=mirror printing.
    types::VariousModeSettingsFields vmsFields{};
    vmsFields.autoCut     = true;
    vmsFields.mirrorPrint = false;
    types::VariousModeSettings settings{};
    settings.set_fields(vmsFields);

    commands::VariousModeSettings cmd(settings);
    const auto result = cmd.get();

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == 0x1B);
    REQUIRE(result[1] == 0x69);
    REQUIRE(result[2] == 0x4D);
    // bit 6 set: 01000000 = 0x40
    REQUIRE(result[3] == 0x40);
}

// ---------------------------------------------------------------------------
// lineWidth constant – must be 16 for all variants
// ---------------------------------------------------------------------------

TEST_CASE("lineWidth - all variants use 16 bytes per line", "[pte550w][config]")
{
    REQUIRE(lineWidth.at(types::ModelVariant::PtE550W)  == 16);
    REQUIRE(lineWidth.at(types::ModelVariant::PtP750W)  == 16);
    REQUIRE(lineWidth.at(types::ModelVariant::PtP710BT) == 16);
}

// ---------------------------------------------------------------------------
// Resolution – all variants expose 180 dpi
// ---------------------------------------------------------------------------

TEST_CASE("resolutions - all variants are 180 dpi", "[pte550w][config]")
{
    for (const auto& [variant, res] : resolutions)
    {
        REQUIRE(res.x == 180);
        REQUIRE(res.y == 180);
    }
}

// ---------------------------------------------------------------------------
// minFeedMarginDots – spec §2.6: 14 dots at 180 dpi
// ---------------------------------------------------------------------------

TEST_CASE("minFeedMarginDots - 14 dots for all variants", "[pte550w][config]")
{
    REQUIRE(minFeedMarginDots.at(types::ModelVariant::PtE550W)  == 14);
    REQUIRE(minFeedMarginDots.at(types::ModelVariant::PtP750W)  == 14);
    REQUIRE(minFeedMarginDots.at(types::ModelVariant::PtP710BT) == 14);
}

// ---------------------------------------------------------------------------
// modelVariantMap – all three model strings must resolve
// ---------------------------------------------------------------------------

TEST_CASE("modelVariantMap - known model strings resolve", "[pte550w][config]")
{
    REQUIRE(modelVariantMap.count("PT-E550W")  == 1);
    REQUIRE(modelVariantMap.count("PT-P750W")  == 1);
    REQUIRE(modelVariantMap.count("PT-P710BT") == 1);
}

TEST_CASE("modelVariantMap - unknown model string is absent", "[pte550w][config]")
{
    REQUIRE(modelVariantMap.count("TD-2020") == 0);
}
