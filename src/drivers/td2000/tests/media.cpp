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
// Created by armin on 15.02.26.
//

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <string_view>

extern "C" {
#include <cups/cups.h>
}

#include "td2000.h"

using namespace drivers::td2000;
using namespace drivers::td2000::media;
using namespace drivers::td2000::types;

// ---------------------------------------------------------------------------
// MediaInfoFields parsing tests
// ---------------------------------------------------------------------------

TEST_CASE("MediaInfoFields - parse continuous tape blob (57mm)", "[media][MediaInfoFields]") {
    const auto fields = td2x2x::_57mm.fields();

    SECTION("sensorId") { REQUIRE(fields.sensorId == 63); }
    SECTION("energyRank") { REQUIRE(fields.energyRank == 4); }
    SECTION("paperWidth is 57mm") { REQUIRE(fields.paperWidth == 57); }
    SECTION("paperLengthMm is 0 for continuous tape") { REQUIRE(fields.paperLengthMm == 0); }
    SECTION("headDivide") { REQUIRE(fields.headDivide == 0); }
    SECTION("rollWidMm") { REQUIRE(fields.rollWidMm == 57); }
    SECTION("pinOffsetLeft") { REQUIRE(fields.pinOffsetLeft == 8); }
    SECTION("imageAreaWidthRes") { REQUIRE(fields.imageAreaWidthRes == 432); }
    SECTION("imageAreaLengthRes is 0 for continuous tape") { REQUIRE(fields.imageAreaLengthRes == 0); }
    SECTION("paperSize") { REQUIRE(fields.paperSize == 0x01B6); }
    SECTION("sizeMM string") { REQUIRE(std::string(fields.sizeMM) == "RD 57mm"); }
    SECTION("sizeIN string") { REQUIRE(std::string(fields.sizeIN) == "2.25\""); }
    SECTION("lblPitchDot is 0 for continuous tape") { REQUIRE(fields.lblPitchDot == 0); }
    SECTION("reserved_12_[6] == 0 marks continuous tape") { REQUIRE(fields.reserved_12_[6] == 0); }
}

TEST_CASE("MediaInfoFields - parse die-cut label blob (40x40mm)", "[media][MediaInfoFields]") {
    const auto fields = td2x2x::_40x40mm.fields();

    SECTION("sensorId") { REQUIRE(fields.sensorId == 63); }
    SECTION("energyRank") { REQUIRE(fields.energyRank == 5); }
    SECTION("paperWidth is 40mm") { REQUIRE(fields.paperWidth == 40); }
    SECTION("paperLengthMm is 40mm for die-cut") { REQUIRE(fields.paperLengthMm == 40); }
    SECTION("rollWidMm") { REQUIRE(fields.rollWidMm == 44); }
    SECTION("pinOffsetLeft") { REQUIRE(fields.pinOffsetLeft == 76); }
    SECTION("imageAreaWidthRes") { REQUIRE(fields.imageAreaWidthRes == 296); }
    SECTION("imageAreaLengthRes") { REQUIRE(fields.imageAreaLengthRes == 272); }
    SECTION("sizeMM string") { REQUIRE(std::string(fields.sizeMM) == "40mm x 40mm"); }
    SECTION("sizeIN string") { REQUIRE(std::string(fields.sizeIN) == "1.5\" x 1.5\""); }
    SECTION("lblPitchDot is non-zero for die-cut") { REQUIRE(fields.lblPitchDot == 348); }
    SECTION("reserved_12_[6] == 1 marks die-cut label") { REQUIRE(fields.reserved_12_[6] == 1); }
}

// ---------------------------------------------------------------------------
// MediaInfo set_fields / round-trip tests
// ---------------------------------------------------------------------------

TEST_CASE("MediaInfo - set_fields round-trip", "[media][MediaInfo]") {
    MediaInfoFields original{};
    original.sensorId           = 63;
    original.energyRank         = 4;
    original.paperWidth         = 57;
    original.paperLengthMm      = 0;
    original.headDivide         = 0;
    original.rollWidMm          = 57;
    original.pinOffsetLeft      = 8;
    original.imageAreaWidthRes  = 432;
    original.imageAreaLengthRes = 0;
    original.paperSize          = 0x01B6;
    original.lblPitchDot        = 0;

    types::MediaInfo info{};
    info.set_fields(original);
    const auto parsed = info.fields();

    SECTION("sensorId survives round-trip") { REQUIRE(parsed.sensorId == original.sensorId); }
    SECTION("paperWidth survives round-trip") { REQUIRE(parsed.paperWidth == original.paperWidth); }
    SECTION("paperLengthMm survives round-trip") { REQUIRE(parsed.paperLengthMm == original.paperLengthMm); }
    SECTION("pinOffsetLeft survives round-trip") { REQUIRE(parsed.pinOffsetLeft == original.pinOffsetLeft); }
    SECTION("imageAreaWidthRes survives round-trip") { REQUIRE(parsed.imageAreaWidthRes == original.imageAreaWidthRes); }
    SECTION("paperSize survives round-trip") { REQUIRE(parsed.paperSize == original.paperSize); }
    SECTION("lblPitchDot survives round-trip") { REQUIRE(parsed.lblPitchDot == original.lblPitchDot); }
}

TEST_CASE("MediaInfo - set_fields writes correct raw bytes", "[media][MediaInfo]") {
    MediaInfoFields fields{};
    fields.paperWidth  = 40;
    fields.pinOffsetLeft = 76;
    fields.imageAreaWidthRes = 296;   // 0x0128 LE → 0x28, 0x01

    types::MediaInfo info{};
    info.set_fields(fields);

    SECTION("paperWidth at byte 2") { REQUIRE(info.raw[2] == 40); }
    SECTION("pinOffsetLeft at byte 6") { REQUIRE(info.raw[6] == 76); }
    SECTION("imageAreaWidthRes low byte at 8") { REQUIRE(info.raw[8] == 0x28); }
    SECTION("imageAreaWidthRes high byte at 9") { REQUIRE(info.raw[9] == 0x01); }
}

TEST_CASE("MediaInfo - two blobs with different reserved_12_[6] compare unequal", "[media][MediaInfo]") {
    const auto continuous = td2x2x::_57mm;
    const auto diecut     = td2x2x::_40x40mm;

    REQUIRE_FALSE(continuous == diecut);
    REQUIRE(continuous.fields().reserved_12_[6] == 0);
    REQUIRE(diecut.fields().reserved_12_[6] == 1);
}

// ---------------------------------------------------------------------------
// AdditionalMediaInformation command tests
// ---------------------------------------------------------------------------

TEST_CASE("AdditionalMediaInformation - command structure", "[media][commands]") {
    // ESC i U: 1Bh 69h 55h 77h 01h + 127-byte payload
    const auto result = commands::AdditionalMediaInformation(td2x2x::_57mm).get();

    SECTION("total size is 132 bytes (5 header + 127 payload)") {
        REQUIRE(result.size() == 132);
    }

    SECTION("header byte 0: ESC (0x1B)") { REQUIRE(result[0] == 0x1B); }
    SECTION("header byte 1: i   (0x69)") { REQUIRE(result[1] == 0x69); }
    SECTION("header byte 2: U   (0x55)") { REQUIRE(result[2] == 0x55); }
    SECTION("header byte 3: length (0x77)") { REQUIRE(result[3] == 0x77); }
    SECTION("header byte 4: count (0x01)") { REQUIRE(result[4] == 0x01); }

    SECTION("payload matches raw blob bytes") {
        const auto& raw = td2x2x::_57mm.raw;
        for (size_t i = 0; i < raw.size(); ++i) {
            REQUIRE(result[5 + i] == raw[i]);
        }
    }
}

TEST_CASE("AdditionalMediaInformation - different media produces different payload", "[media][commands]") {
    const auto result57  = commands::AdditionalMediaInformation(td2x2x::_57mm).get();
    const auto result58  = commands::AdditionalMediaInformation(td2x2x::_58mm).get();

    REQUIRE(result57 != result58);
    // Header must be identical
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(result57[i] == result58[i]);
    }
}

// ---------------------------------------------------------------------------
// getMediaInfoForMedia tests
// ---------------------------------------------------------------------------

TEST_CASE("getMediaInfoForMedia - ContinuousLengthTape", "[media]") {

    SECTION("Td2x2x family - width <= 5700 returns 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5700, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_57mm);
    }

    SECTION("Td2x3x family - width <= 5700 returns 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 5700, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x3x::_57mm);
    }

    SECTION("Td2x2x family - width > 5700 returns 58mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5800, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_58mm);
    }

    SECTION("Td2x3x family - width > 5700 returns 58mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 5800, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x3x::_58mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 40mm width", "[media]") {

    SECTION("Td2x2x - 40x40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 4000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x40mm);
    }

    SECTION("Td2x3x - 40x40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 4000, 4000, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x40mm);
    }

    SECTION("Td2x2x - 40x50mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 5000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x50mm);
    }

    SECTION("Td2x3x - 40x50mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 4000, 5000, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x50mm);
    }

    SECTION("Td2x2x - 40x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 6000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x60mm);
    }

    SECTION("Td2x3x - 40x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 4000, 6000, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x60mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 50mm width", "[media]") {

    SECTION("Td2x2x - 50x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5000, 3000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_50x30mm);
    }

    SECTION("Td2x3x - 50x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 5000, 3000, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_50x30mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 51mm width", "[media]") {

    SECTION("Td2x2x - 51x26mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5100, 2600, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_51x26mm);
    }

    SECTION("Td2x3x - 51x26mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 5100, 2600, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_51x26mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 60mm width", "[media]") {

    SECTION("Td2x2x - 60x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 6000, 6000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_60x60mm);
    }

    SECTION("Td2x3x - 60x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 6000, 6000, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_60x60mm);
    }
}

TEST_CASE("getMediaInfoForMedia - Default fallback", "[media]") {

    SECTION("Unknown media type returns default") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 1000, 1000, MediaType::NoMedia);
        REQUIRE(result == none);
    }

    SECTION("Out of range dimensions returns default") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 7000, 7000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_58mm);
    }
}

TEST_CASE("getMediaInfoForMedia - Boundary conditions", "[media][boundary]") {

    SECTION("ContinuousTape - width exactly 5700") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5700, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_57mm);
    }

    SECTION("ContinuousTape - width just above 5700") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 5701, 0, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_58mm);
    }

    SECTION("DieCut - width exactly at boundary 4000") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 4000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x40mm);
    }

    SECTION("DieCut - width just above 4000") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4001, 3000, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_50x30mm);
    }

    SECTION("DieCut - length boundary at 4000 vs 4001") {
        auto at_boundary = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 4000, MediaType::DieCutLabels);
        auto above_boundary = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 4000, 4001, MediaType::DieCutLabels);

        REQUIRE(at_boundary == td2x2x::_40x40mm);
        REQUIRE(above_boundary == td2x2x::_40x50mm);
    }
}

// ---------------------------------------------------------------------------
// defaultMedia and mediaTypes configuration tests
// ---------------------------------------------------------------------------

// PAPPL's validate_driver() calls pwgMediaForPWG() on every entry in
// driverData->media and returns false (→ EINVAL → printer creation fails) for
// any entry that resolves to NULL.
TEST_CASE("defaultMedia - all entries resolve via pwgMediaForPWG", "[config]") {
    for (const char *name : defaultMedia) {
        CAPTURE(name);
        REQUIRE(pwgMediaForPWG(name) != nullptr);
    }
}

// PAPPL's validate_ready() rejects media whose size_length < min_length
// computed from the media list.  Any non-range entry with length=0 would be
// filtered by Kate/Qt (y-dimension=0 hides the size in the print dialog).
TEST_CASE("defaultMedia - individual (non-range) entries have non-zero dimensions", "[config]") {
    // The range-defining entries are consumed by PAPPL to build media-col-database
    // range entries and are never emitted as individual named media sizes.
    auto isRangeName = [](std::string_view n) {
        return n.starts_with("roll_min_")   || n.starts_with("roll_max_") ||
               n.starts_with("custom_min_") || n.starts_with("custom_max_");
    };

    for (const char *name : defaultMedia) {
        if (isRangeName(name))
            continue;
        CAPTURE(name);
        const pwg_media_t *pwg = pwgMediaForPWG(name);
        REQUIRE(pwg != nullptr);
        REQUIRE(pwg->width  > 0);
        REQUIRE(pwg->length > 0);
    }
}

// defaultMedia[0] is used for both media_ready and media_default.
// Its dimensions must be within the [roll_min, roll_max] range or
// papplPrinterSetDriverData / validate_ready will reject the printer.
TEST_CASE("defaultMedia - first entry is suitable for media_ready", "[config]") {
    const pwg_media_t *ready = pwgMediaForPWG(defaultMedia[0]);
    REQUIRE(ready != nullptr);

    const pwg_media_t *minPwg = pwgMediaForPWG("roll_min_57x12mm");
    const pwg_media_t *maxPwg = pwgMediaForPWG("roll_max_58x1000mm");
    REQUIRE(minPwg != nullptr);
    REQUIRE(maxPwg != nullptr);

    REQUIRE(ready->width  >= minPwg->width);
    REQUIRE(ready->width  <= maxPwg->width);
    REQUIRE(ready->length >= minPwg->length);
    REQUIRE(ready->length <= maxPwg->length);
}

// startPage() maps media-type strings to MediaType enum values.
// All three variants must be present so the printer correctly handles both
// continuous tape and die-cut label jobs.
TEST_CASE("mediaTypes - required type strings are present", "[config]") {
    auto has = [](std::string_view needle) {
        return std::ranges::any_of(mediaTypes, [&](const char *t) {
            return std::string_view(t) == needle;
        });
    };

    SECTION("'continuous' is present (ContinuousLengthTape)") {
        REQUIRE(has("continuous"));
    }
    SECTION("'labels' is present (DieCutLabels)") {
        REQUIRE(has("labels"));
    }
    SECTION("'labels-continuous' is present (ContinuousLengthTape)") {
        REQUIRE(has("labels-continuous"));
    }
}