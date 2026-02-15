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

#include "td2000.h"

using namespace drivers::td2000;
using namespace drivers::td2000::media;
using namespace drivers::td2000::types;

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
        REQUIRE(result == td2x2x::_58mm);
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