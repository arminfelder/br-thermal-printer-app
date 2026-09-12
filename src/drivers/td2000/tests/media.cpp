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

using mp_units::si::unit_symbols::mm;
// PAPPL supplies media dimensions in hundredths of a millimetre.
using util::units::fromPwg;

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
    SECTION("mediaClass == 0 marks continuous tape") { REQUIRE(fields.mediaClass == 0); }
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
    SECTION("mediaClass == 1 marks die-cut label") { REQUIRE(fields.mediaClass == 1); }
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

TEST_CASE("MediaInfo - two blobs with different mediaClass compare unequal", "[media][MediaInfo]") {
    const auto continuous = td2x2x::_57mm;
    const auto diecut     = td2x2x::_40x40mm;

    REQUIRE_FALSE(continuous == diecut);
    REQUIRE(continuous.fields().mediaClass == 0);
    REQUIRE(diecut.fields().mediaClass == 1);
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

    SECTION("Td2x2x family - width <= 57mm returns 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 57 * mm, 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_57mm);
    }

    SECTION("Td2x3x family - width <= 57mm returns 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 57 * mm, 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x3x::_57mm);
    }

    SECTION("Td2x2x family - width > 57mm returns 58mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 58 * mm, 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_58mm);
    }

    SECTION("Td2x3x family - width > 57mm returns 58mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 58 * mm, 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x3x::_58mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 40mm width", "[media]") {

    SECTION("Td2x2x - 40x40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, 40 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x40mm);
    }

    SECTION("Td2x3x - 40x40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 40 * mm, 40 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x40mm);
    }

    SECTION("Td2x2x - 40x50mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, 50 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x50mm);
    }

    SECTION("Td2x3x - 40x50mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 40 * mm, 50 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x50mm);
    }

    SECTION("Td2x2x - 40x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, 60 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x60mm);
    }

    SECTION("Td2x3x - 40x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 40 * mm, 60 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_40x60mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 50mm width", "[media]") {

    SECTION("Td2x2x - 50x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 50 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_50x30mm);
    }

    SECTION("Td2x3x - 50x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 50 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_50x30mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 51mm width", "[media]") {

    SECTION("Td2x2x - 51x26mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 51 * mm, 26 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_51x26mm);
    }

    SECTION("Td2x3x - 51x26mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 51 * mm, 26 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_51x26mm);
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 60mm width", "[media]") {

    SECTION("Td2x2x - 60x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 60 * mm, 60 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_60x60mm);
    }

    SECTION("Td2x3x - 60x60mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 60 * mm, 60 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_60x60mm);
    }
}

TEST_CASE("MediaInfoFields - print area matches raster spec 2.3.2 (b)", "[media][MediaInfoFields]") {
    // Columns 3 and 4 of the die-cut label table, Raster Command Reference v1.01 page 13.
    struct SpecRow { const types::MediaInfo& blob; uint16_t id; uint16_t printAreaWidth; uint16_t printAreaLength; };

    SECTION("Td2x2x at 203 dpi") {
        const std::array rows{
            SpecRow{td2x2x::_51x26mm, 422, 382, 157},
            SpecRow{td2x2x::_30x30mm, 431, 216, 192},
            SpecRow{td2x2x::_40x40mm, 432, 296, 272},
            SpecRow{td2x2x::_40x50mm, 433, 296, 352},
            SpecRow{td2x2x::_40x60mm, 434, 296, 432},
            SpecRow{td2x2x::_50x30mm, 435, 376, 192},
            SpecRow{td2x2x::_60x60mm, 437, 448, 432},
        };
        for (const auto& row : rows) {
            const auto f = row.blob.fields();
            REQUIRE(f.paperSize == row.id);
            REQUIRE(f.imageAreaWidthRes == row.printAreaWidth);
            REQUIRE(f.imageAreaLengthRes == row.printAreaLength);
            // Length offset column, constant per family.
            REQUIRE(f.lengthOffsetDots1 == 24);
            REQUIRE(f.lengthOffsetDots2 == 24);
        }
    }

    SECTION("Td2x3x at 300 dpi") {
        const std::array rows{
            SpecRow{td2x3x::_51x26mm, 422, 564, 231},
            SpecRow{td2x3x::_30x30mm, 431, 318, 283},
            SpecRow{td2x3x::_40x40mm, 432, 436, 401},
            SpecRow{td2x3x::_40x50mm, 433, 436, 519},
            SpecRow{td2x3x::_40x60mm, 434, 436, 638},
        };
        for (const auto& row : rows) {
            const auto f = row.blob.fields();
            REQUIRE(f.paperSize == row.id);
            REQUIRE(f.imageAreaWidthRes == row.printAreaWidth);
            REQUIRE(f.imageAreaLengthRes == row.printAreaLength);
            REQUIRE(f.lengthOffsetDots1 == 35);
            REQUIRE(f.lengthOffsetDots2 == 35);
        }
    }

    SECTION("Continuous tape carries no label geometry") {
        for (const auto& blob : {td2x2x::_57mm, td2x2x::_58mm, td2x3x::_57mm, td2x3x::_58mm}) {
            const auto f = blob.fields();
            REQUIRE(f.paperLengthMm == 0);
            REQUIRE(f.imageAreaLengthRes == 0);
            REQUIRE(f.lblPitchDot == 0);
            REQUIRE(f.mediaClass == 0);
        }
    }
}

TEST_CASE("getMediaInfoForMedia - DieCutLabels 30mm width", "[media]") {

    SECTION("Td2x2x - 30x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 30 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_30x30mm);
    }

    SECTION("Td2x3x - 30x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x3x, 30 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x3x::_30x30mm);
    }

    SECTION("30x30mm blob is not the 40x40mm blob") {
        REQUIRE_FALSE(td2x2x::_30x30mm == td2x2x::_40x40mm);
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 30 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE_FALSE(result == td2x2x::_40x40mm);
    }
}

TEST_CASE("getMediaInfoForMedia - length is not ignored above 40mm width", "[media]") {

    SECTION("50mm wide and 20mm long fits the 50x30mm label") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 50 * mm, 20 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_50x30mm);
    }

    SECTION("50mm wide and 55mm long does not fit 50x30mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 50 * mm, 55 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_60x60mm);
    }

    SECTION("51x30mm has no own media and uses the next label that fits") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 51 * mm, 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_60x60mm);
    }
}

TEST_CASE("MediaInfoFields - every blob has a distinct identity", "[media][MediaInfoFields]") {
    // Guards against a copied blob. td2x2x/51x30mm.bin was a byte copy of
    // td2x2x/30x30mm.bin and announced the wrong size.
    const std::array blobs{
        td2x2x::_30x30mm, td2x2x::_40x40mm, td2x2x::_40x50mm, td2x2x::_40x60mm,
        td2x2x::_50x30mm, td2x2x::_51x26mm, td2x2x::_57mm, td2x2x::_58mm,
        td2x2x::_60x60mm,
    };

    for (std::size_t i = 0; i < blobs.size(); ++i) {
        for (std::size_t j = i + 1; j < blobs.size(); ++j) {
            REQUIRE_FALSE(blobs[i] == blobs[j]);
            REQUIRE(blobs[i].fields().paperSize != blobs[j].fields().paperSize);
        }
    }
}

TEST_CASE("printInfoMedia - ESC i z agrees with the ESC i U record", "[media][printInfo]") {
    // The printer compares the {n3}/{n4} of ESC i z against the media record of ESC i U.
    // A request that is smaller than the selected label must not leak into ESC i z.

    SECTION("die-cut request smaller than the selected label") {
        const auto info = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 30 * mm, 20 * mm, MediaType::DieCutLabels);
        REQUIRE(info == td2x2x::_30x30mm);

        const auto pi = printInfoMedia(info, MediaType::DieCutLabels, 20 * mm);
        REQUIRE(pi.width == info.fields().paperWidth);
        REQUIRE(pi.length == info.fields().paperLengthMm);
        REQUIRE(pi.length == 30);        // the label, not the requested 20
        REQUIRE(pi.lengthValid);
    }

    SECTION("every die-cut medium reports its own dimensions") {
        for (const auto& blob : {td2x2x::_30x30mm, td2x2x::_40x40mm, td2x2x::_40x50mm,
                                 td2x2x::_40x60mm, td2x2x::_50x30mm, td2x2x::_51x26mm,
                                 td2x2x::_60x60mm}) {
            const auto f = blob.fields();
            const auto pi = printInfoMedia(blob, MediaType::DieCutLabels, 1 * mm);
            REQUIRE(pi.width == f.paperWidth);
            REQUIRE(pi.length == f.paperLengthMm);
        }
    }

    SECTION("continuous request narrower than the tape reports the tape width") {
        const auto info = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 50 * mm, 100 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(info == td2x2x::_57mm);

        const auto pi = printInfoMedia(info, MediaType::ContinuousLengthTape, 100 * mm);
        REQUIRE(pi.width == 57);         // the tape, not the requested 50
        REQUIRE(pi.length == 100);       // the job length
        REQUIRE(pi.lengthValid);
    }

    SECTION("continuous length above 255mm cannot use one byte") {
        const auto pi = printInfoMedia(td2x2x::_58mm, MediaType::ContinuousLengthTape, 1000 * mm);
        REQUIRE(pi.width == 58);
        REQUIRE_FALSE(pi.lengthValid);
        REQUIRE(pi.length == 0);         // never a truncated 1000 & 0xFF == 232
    }

    SECTION("continuous length at the one-byte boundary") {
        const auto at = printInfoMedia(td2x2x::_58mm, MediaType::ContinuousLengthTape, 255 * mm);
        REQUIRE(at.lengthValid);
        REQUIRE(at.length == 255);
        const auto over = printInfoMedia(td2x2x::_58mm, MediaType::ContinuousLengthTape, 256 * mm);
        REQUIRE_FALSE(over.lengthValid);
    }
}

TEST_CASE("getMediaInfoForMedia - Default fallback", "[media]") {

    SECTION("Unknown media type returns default") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 10 * mm, 10 * mm, MediaType::NoMedia);
        REQUIRE(result == none);
    }

    SECTION("Out of range dimensions returns default") {
        // No die-cut media is larger than 60x60mm. Continuous tape is not a
        // substitute for a die-cut request, so the lookup must report none.
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 70 * mm, 70 * mm, MediaType::DieCutLabels);
        REQUIRE(result == none);
    }
}

TEST_CASE("getMediaInfoForMedia - Boundary conditions", "[media][boundary]") {

    SECTION("ContinuousTape - width exactly 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 57 * mm, 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_57mm);
    }

    SECTION("ContinuousTape - width just above 57mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, fromPwg(5701), 0 * mm, MediaType::ContinuousLengthTape);
        REQUIRE(result == td2x2x::_58mm);
    }

    SECTION("DieCut - width exactly at boundary 40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, 40 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_40x40mm);
    }

    SECTION("DieCut - width just above 40mm") {
        auto result = getMediaInfoForMedia(
            ModelFamily::Td2x2x, fromPwg(4001), 30 * mm, MediaType::DieCutLabels);
        REQUIRE(result == td2x2x::_50x30mm);
    }

    SECTION("DieCut - length boundary at 40mm vs 40.01mm") {
        auto at_boundary = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, 40 * mm, MediaType::DieCutLabels);
        auto above_boundary = getMediaInfoForMedia(
            ModelFamily::Td2x2x, 40 * mm, fromPwg(4001), MediaType::DieCutLabels);

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