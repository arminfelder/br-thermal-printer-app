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
#include <cstdint>
#include <cups/cups.h>
#include <string_view>
#include <vector>
#include "pte550w.h"
#include "units.h"

using namespace drivers::pte550w;

// ---------------------------------------------------------------------------
// SwitchDynamicCommandMode
// ---------------------------------------------------------------------------

TEST_CASE("SwitchDynamicCommandMode - raster mode", "[pte550w][commands]")
{
    commands::SwitchDynamicCommandMode const cmd(types::CommandMode::Raster);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x1B, 0x69, 0x61, 0x01};
    REQUIRE(result == expected);
}

// ---------------------------------------------------------------------------
// SelectCompressionMode – TIFF PackBits is mandatory for this family
// ---------------------------------------------------------------------------

TEST_CASE("SelectCompressionMode - compression enabled", "[pte550w][commands]")
{
    commands::SelectCompressionMode const cmd(types::CompressionMode::Compression);
    const auto result = cmd.get();
    const std::vector<uint8_t> expected = {0x4D, 0x02};
    REQUIRE(result == expected);
}

TEST_CASE("SelectCompressionMode - no compression", "[pte550w][commands]")
{
    commands::SelectCompressionMode const cmd(types::CompressionMode::NoCompression);
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
    commands::RasterGraphicsTransfer const cmd(16, rasterData);
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
    commands::RasterGraphicsTransfer const cmd(257, rasterData);
    const auto result = cmd.get();

    REQUIRE(result[0] == 0x47);
    REQUIRE(result[1] == 0x01); // 257 % 256 = 1
    REQUIRE(result[2] == 0x01); // 257 / 256 = 1
    REQUIRE(result.size() == 3 + 257);
}

TEST_CASE("RasterGraphicsTransfer - empty raster data", "[pte550w][commands]")
{
    const std::vector<uint8_t> empty;
    commands::RasterGraphicsTransfer const cmd(0, empty);
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
    commands::StatusInformationRequest const cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 0x1B);
    REQUIRE(result[1] == 0x69);
    REQUIRE(result[2] == 0x53);
}

// ---------------------------------------------------------------------------
// PrintInformation
// ---------------------------------------------------------------------------

TEST_CASE("printInformation - same flags as the spec sample for 24 mm tape", "[pte550w][commands]")
{
    // Spec §2.2.3 p.11: 1B 69 7A 84 00 18 00 AA 02 00 00 ...
    // PI_RECOVER | PI_WIDTH only. PI_KIND is not set, thus any TZe tape type is accepted.
    const auto result = commands::PrintInformation{
        printInformation(24, 682, types::PageType::startingPage)}.get();

    const std::vector<uint8_t> expected = {
        0x1B, 0x69, 0x7A, 0x84, 0x00, 0x18, 0x00, 0xAA, 0x02, 0x00, 0x00, 0x00, 0x00};
    REQUIRE(result == expected);
}

TEST_CASE("printInformation - {n10} is fixed at 0 and n9 marks other pages", "[pte550w][commands]")
{
    // Spec p.32: {n9} starting page 0, other pages 1. {n10} fixed at 0.
    const auto info = printInformation(12, 100, types::PageType::otherPage);
    REQUIRE(info.pageType == types::PageType::otherPage);
    REQUIRE(info.quality == 0x00);
    REQUIRE(info.mediaWidth == 12);
}

// ---------------------------------------------------------------------------
// PrintWithFeeding
// ---------------------------------------------------------------------------

TEST_CASE("PrintWithFeeding - correct byte", "[pte550w][commands]")
{
    commands::PrintWithFeeding const cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x1A);
}

// ---------------------------------------------------------------------------
// Print (mid-job page separator)
// ---------------------------------------------------------------------------

TEST_CASE("Print - correct byte", "[pte550w][commands]")
{
    commands::Print const cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x0C);
}

// ---------------------------------------------------------------------------
// ZeroRasterGraphics
// ---------------------------------------------------------------------------

TEST_CASE("ZeroRasterGraphics - correct byte", "[pte550w][commands]")
{
    commands::ZeroRasterGraphics const cmd;
    const auto result = cmd.get();
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0x5A);
}

// ---------------------------------------------------------------------------
// SpecifyMarginAmount – spec §2.3.3 p.17, ESC i d p.34
// ---------------------------------------------------------------------------

TEST_CASE("SpecifyMarginAmount - 14 dot feed margin", "[pte550w][commands]")
{
    const std::vector<uint8_t> expected = {0x1B, 0x69, 0x64, 0x0E, 0x00};
    REQUIRE(commands::SpecifyMarginAmount{14}.get() == expected);
}

TEST_CASE("SpecifyMarginAmount - 1800 dot maximum at 180x360 dpi", "[pte550w][commands]")
{
    const std::vector<uint8_t> expected = {0x1B, 0x69, 0x64, 0x08, 0x07};
    REQUIRE(commands::SpecifyMarginAmount{1800}.get() == expected);
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

    commands::VariousModeSettings const cmd(settings);
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
// Resolution – spec §2.3.1 p.13
// ---------------------------------------------------------------------------

// 180x360 dpi works on the PT-P750W, but the image filter of PAPPL prints a PNG at half
// its length at that resolution. Thus only 180x180 dpi is advertised.
TEST_CASE("resolutions - only 180x180 is advertised", "[pte550w][config]")
{
    REQUIRE(resolutions.size() == 1);
    REQUIRE(resolutions[0].x == 180);
    REQUIRE(resolutions[0].y == 180);
}

TEST_CASE("updateDriverData - every model advertises 180x180 only", "[pte550w][config]")
{
    for (const auto* model : {"PT-E550W", "PT-P750W", "PT-P710BT"})
    {
        pappl_pr_driver_data_t data{};
        INFO(model);
        REQUIRE(updateDriverData(&data, model));
        REQUIRE(data.num_resolution == 1);
        REQUIRE(data.x_resolution[0] == 180);
        REQUIRE(data.y_resolution[0] == 180);
        REQUIRE(data.x_default == 180);
        REQUIRE(data.y_default == 180);
    }
}

// ---------------------------------------------------------------------------
// pageSettings – spec §2.3.3 p.17, §2.3.4 p.18, ESC i K p.33, §3 p.22
// ---------------------------------------------------------------------------

TEST_CASE("pageSettings - 180 dpi feed: normal resolution, 14 dot margin", "[pte550w][page]")
{
    const auto page = pageSettings(types::ModelVariant::PtP750W, 180, 1000);
    REQUIRE_FALSE(page.highResolution);
    REQUIRE(page.feedMarginDots == 14);
    REQUIRE(page.rasterLines == 1000);
}

TEST_CASE("pageSettings - 360 dpi feed: high resolution, 28 dot margin", "[pte550w][page]")
{
    for (const auto variant : {types::ModelVariant::PtE550W, types::ModelVariant::PtP750W,
                               types::ModelVariant::PtP710BT})
    {
        const auto page = pageSettings(variant, 360, 2000);
        REQUIRE(page.highResolution);
        REQUIRE(page.feedMarginDots == 28);
        REQUIRE(page.rasterLines == 2000);
    }
}

TEST_CASE("pageSettings - raster lines are clamped to the maximum length", "[pte550w][page]")
{
    // 1000 mm = 7086 dots at 180 dpi, 14172 dots at 360 dpi.
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 180, 7087).rasterLines == 7086);
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 360, 14173).rasterLines == 14172);
}

TEST_CASE("pageSettings - ESC i A is not sent to the PT-P710BT", "[pte550w][page]")
{
    REQUIRE(pageSettings(types::ModelVariant::PtE550W, 180, 100).sendCutEvery);
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 180, 100).sendCutEvery);
    REQUIRE_FALSE(pageSettings(types::ModelVariant::PtP710BT, 180, 100).sendCutEvery);
}

namespace
{
    struct ModeWire
    {
        uint8_t various;    // ESC i M {n1}
        uint8_t advanced;   // ESC i K {n1}
        bool operator==(const ModeWire&) const = default;
    };

    ModeWire modeWire(const types::CutMode mode, const bool chain,
                      const types::ModelVariant variant = types::ModelVariant::PtP750W, const bool high = false)
    {
        const auto bytes = buildModeBytes(mode, chain, variant, high);
        return {commands::VariousModeSettings{bytes.various}.get().at(3),
                commands::AdvancedModeSettings{bytes.advanced}.get().at(3)};
    }
}

// The wire values in the comments were seen on a PT-P750W with 24 mm TZe tape (2026-09-13).
TEST_CASE("buildModeBytes - cut modes give the ESC i M and ESC i K bits", "[pte550w][cut]")
{
    using types::CutMode;
    REQUIRE(modeWire(CutMode::Full, false) == ModeWire{0x40, 0x08});
    REQUIRE(modeWire(CutMode::Half, false) == ModeWire{0x40, 0x0C});   // laminate cut, backing not cut
    REQUIRE(modeWire(CutMode::None, false) == ModeWire{0x00, 0x08});
    REQUIRE(modeWire(CutMode::SpecialTape, false) == ModeWire{0x00, 0x18});
}

TEST_CASE("buildModeBytes - chain printing clears ESC i K bit 3", "[pte550w][cut]")
{
    using types::CutMode;
    REQUIRE(modeWire(CutMode::Full, true) == ModeWire{0x40, 0x00});
    REQUIRE(modeWire(CutMode::None, true) == ModeWire{0x00, 0x00});    // no cut and no feed at the end
}

TEST_CASE("buildModeBytes - the PT-P710BT has no half cut", "[pte550w][cut]")
{
    REQUIRE(modeWire(types::CutMode::Half, false, types::ModelVariant::PtP710BT) == ModeWire{0x40, 0x08});
}

TEST_CASE("buildModeBytes - bit 6 is set only for high resolution", "[pte550w][cut]")
{
    REQUIRE(modeWire(types::CutMode::Full, false, types::ModelVariant::PtP750W, true) == ModeWire{0x40, 0x48});
}

TEST_CASE("parseCutMode - each keyword of the supported list has a mode and a name", "[pte550w][cut]")
{
    for (const char* keyword : vendorOptions::cutModeValues)
    {
        const auto mode = parseCutMode(keyword);
        REQUIRE(mode.has_value());
        REQUIRE(std::string_view(cutModeName(*mode)) == keyword);
    }
    REQUIRE_FALSE(parseCutMode("fill").has_value());
}

TEST_CASE("pageSettings - brother-margin is millimetres and doubles at 360 dpi", "[pte550w][page]")
{
    const auto variant = types::ModelVariant::PtP750W;
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 2}).feedMarginDots == 14);
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 127}).feedMarginDots == 900);
    REQUIRE(pageSettings(variant, 360, 100, {.marginMm = 127}).feedMarginDots == 1800);
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 0}).feedMarginDots == 14);
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 500}).feedMarginDots == 900);
}

// The paper table of the PT-P750W firmware: nMinNoCutFeed_dot = 175 for each TZe tape.
TEST_CASE("pageSettings - 25 mm is the first whole millimetre without a precut", "[pte550w][page]")
{
    const auto variant = types::ModelVariant::PtP750W;
    REQUIRE(vendorOptions::noPrecutFeedDots == 175);
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 24}).feedMarginDots < vendorOptions::noPrecutFeedDots);
    REQUIRE(pageSettings(variant, 180, 100, {.marginMm = 25}).feedMarginDots >= vendorOptions::noPrecutFeedDots);
}

TEST_CASE("pageSettings - brother-cut-every goes to ESC i A", "[pte550w][page]")
{
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 180, 100).cutEvery == 1);
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 180, 100, {.cutEvery = 5}).cutEvery == 5);
    REQUIRE(pageSettings(types::ModelVariant::PtP750W, 180, 100, {.cutEvery = 500}).cutEvery == 99);
}

// Spec §2.3.5 pin table: left margin + print area + right margin = 128 pins.
TEST_CASE("tapes - the side margin agrees with the pin table of the spec", "[pte550w][tape]")
{
    struct Row { uint8_t statusWidth; int tapeDots; int printPins; };
    // Tape width in dots from spec §2.3.2 and the firmware paper table (nPaperWidthRes_dot).
    for (const auto row : {Row{4, 24, 24}, Row{6, 42, 32}, Row{9, 64, 50}, Row{12, 84, 70}, Row{18, 128, 112},
                           Row{24, 170, 128}})
    {
        const auto tape = tapeForStatusWidth(row.statusWidth);
        REQUIRE(tape.has_value());
        REQUIRE(tape->sideMarginDots == (row.tapeDots - row.printPins) / 2);
    }
    REQUIRE_FALSE(tapeForStatusWidth(0).has_value());
    REQUIRE_FALSE(tapeForStatusWidth(36).has_value());
}

TEST_CASE("tapeWidthCode - the 3.5 mm tape is 4 in ESC i z", "[pte550w][tape]")
{
    using util::units::fromPwg;
    REQUIRE(tapeWidthCode(fromPwg(350)) == 4);
    REQUIRE(tapeWidthCode(fromPwg(340)) == 4);
    REQUIRE(tapeWidthCode(fromPwg(600)) == 6);
    REQUIRE(tapeWidthCode(fromPwg(1200)) == 12);
    REQUIRE(tapeWidthCode(fromPwg(2400)) == 24);
}

TEST_CASE("sideMargin - 24 mm tape: 21 dots, not less than 2.96 mm", "[pte550w][tape]")
{
    REQUIRE(util::units::toPwg(sideMargin(*tapeForStatusWidth(24))) == 297);
    REQUIRE(util::units::toPwg(sideMargin(*tapeForStatusWidth(12))) == 99);
    REQUIRE(util::units::toPwg(sideMargin(*tapeForStatusWidth(4))) == 0);
}

TEST_CASE("updateDriverData - vendor options and the 24 mm default margins", "[pte550w][config]")
{
    pappl_pr_driver_data_t data{};
    ipp_t* attrs = nullptr;
    REQUIRE(updateDriverData(&data, "PT-P750W", &attrs));
    REQUIRE(data.num_vendor == 4);
    REQUIRE(attrs != nullptr);
    REQUIRE(ippFindAttribute(attrs, "brother-cut-mode-supported", IPP_TAG_KEYWORD) != nullptr);
    REQUIRE(ippGetInteger(ippFindAttribute(attrs, "brother-margin-default", IPP_TAG_INTEGER), 0) == 2);
    ippDelete(attrs);

    REQUIRE(data.media_default.left_margin == 297);
    REQUIRE(data.media_ready[0].right_margin == 297);
    REQUIRE(std::string_view(data.media_default.source) == "main-roll");
}

TEST_CASE("defaultMedia - minimum length is 4.4 mm", "[pte550w][config]")
{
    // Spec §2.3.4 p.18: minimum length 4.4 mm (31 dots at 180 dpi).
    const auto* min = pwgMediaForPWG(defaultMedia[1]);
    REQUIRE(min != nullptr);
    REQUIRE(min->length == 440);
    // The narrowest TZe tape is 3.5 mm.
    REQUIRE(min->width == 350);
}

// ---------------------------------------------------------------------------
// minFeedMargin – spec §2.3.3 p.17: 14 dots at 180 dpi
// ---------------------------------------------------------------------------

TEST_CASE("minFeedMargin - 14 dots for all variants", "[pte550w][config]")
{
    using util::units::dot;
    REQUIRE(minFeedMargin.at(types::ModelVariant::PtE550W)  == 14 * dot);
    REQUIRE(minFeedMargin.at(types::ModelVariant::PtP750W)  == 14 * dot);
    REQUIRE(minFeedMargin.at(types::ModelVariant::PtP710BT) == 14 * dot);
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

// ---------------------------------------------------------------------------
// Status block – spec §4 p.24-29
// ---------------------------------------------------------------------------

namespace
{
    types::PrinterInfo statusBlock(std::initializer_list<std::pair<size_t, uint8_t>> bytes)
    {
        types::PrinterInfo info{};
        info.raw[0] = 0x80;
        info.raw[1] = 0x20;
        for (const auto& [offset, value] : bytes)
            info.raw.at(offset) = value;
        return info;
    }
}

TEST_CASE("status - phase number offset 20 is the high byte", "[pte550w][status]")
{
    // Spec p.24: offset 20 = phase number (higher order), offset 21 = lower order.
    const auto fields = statusBlock({{20, 0x00}, {21, 0x14}}).fields();
    REQUIRE(fields.phaseNumberHigh == 0x00);
    REQUIRE(fields.phaseNumberLow == 0x14);
}

TEST_CASE("status - notification numbers follow table 7", "[pte550w][status]")
{
    REQUIRE(statusBlock({{22, 0x01}}).fields().notificationNumber == types::NotificationNumber::CoverOpen);
    REQUIRE(statusBlock({{22, 0x02}}).fields().notificationNumber == types::NotificationNumber::CoverClosed);
}

TEST_CASE("status - model codes follow table 4", "[pte550w][status]")
{
    REQUIRE(statusBlock({{4, 0x66}}).fields().model == types::ModelCode::PtE550W);
    REQUIRE(statusBlock({{4, 0x68}}).fields().model == types::ModelCode::PtP750W);
}

TEST_CASE("status - heat-shrink tube 3:1 media type", "[pte550w][status]")
{
    REQUIRE(statusBlock({{11, 0x17}}).fields().mediaType == types::MediaType::HeatShrinkTube3To1);
}

TEST_CASE("statusReasons - cover open notification sets cover-open", "[pte550w][status]")
{
    const auto info = statusBlock({{18, 0x05}, {22, 0x01}});
    REQUIRE(statusReasons(info) == PAPPL_PREASON_COVER_OPEN);
}

TEST_CASE("statusReasons - cover closed notification clears the reasons", "[pte550w][status]")
{
    REQUIRE(statusReasons(statusBlock({{18, 0x05}, {22, 0x02}})) == PAPPL_PREASON_NONE);
}

TEST_CASE("statusReasons - error bits count in a reply to a status request", "[pte550w][status]")
{
    // Status type 00h, error information 1 bit 0 = no media.
    const auto reasons = statusReasons(statusBlock({{18, 0x00}, {8, 0x01}}));
    REQUIRE((reasons & PAPPL_PREASON_MEDIA_EMPTY) != 0);
}

TEST_CASE("statusReasons - turned off is offline", "[pte550w][status]")
{
    REQUIRE(statusReasons(statusBlock({{18, 0x04}})) == PAPPL_PREASON_OFFLINE);
}

TEST_CASE("pausesPrinting - the PT family has no cooling notification", "[pte550w][status]")
{
    // Table 7 has only 00h, 01h and 02h. 03h is cooling on the TD family only.
    REQUIRE_FALSE(pausesPrinting(statusBlock({{18, 0x05}, {22, 0x03}})));
    REQUIRE_FALSE(pausesPrinting(statusBlock({{18, 0x05}, {22, 0x01}})));
}

TEST_CASE("usb productId - Appendix USB specification", "[pte550w][usb]")
{
    REQUIRE(usb::productId.at(types::ModelVariant::PtE550W)  == 0x2060);
    REQUIRE(usb::productId.at(types::ModelVariant::PtP750W)  == 0x2062);
    REQUIRE(usb::productId.at(types::ModelVariant::PtP710BT) == 0x20AF);
}
