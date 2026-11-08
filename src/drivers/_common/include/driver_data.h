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

#ifndef BR_THERMAL_DRIVER_DATA_H
#define BR_THERMAL_DRIVER_DATA_H

#include <span>
#include <string_view>

extern "C" {
#include <pappl/pappl.h>
}

#include "brother_raster.h"
#include "units.h"

namespace brother
{
    // PAPPL reads pappl_media_col_s margins as hundredths of a millimetre,
    // not as printer dots. Feed margins sent by ESC i d are dots.
    struct Margins
    {
        util::units::MediaSize left{};
        util::units::MediaSize right{};
        util::units::MediaSize top{};
        util::units::MediaSize bottom{};
    };

    // The PAPPL driver data that changes between drivers and models. The first
    // entry of each list is the default.
    struct DriverDataSpec
    {
        std::string_view                 model;       // IEEE 1284 MDL, for example "TD-2020"
        pappl_kind_t                     kind;
        std::span<const raster::types::Resolution> resolutions;  // first entry is the default
        Margins                          margins;
        std::span<const char* const>     media;
        std::span<const char* const>     mediaTypes;
        std::span<const char* const>     sources;
    };

    // Fills the driver-independent fields and the fields of `spec`. It does not set the
    // callbacks. It returns false when a list does not fit PAPPL or the default media
    // is not a known PWG size.
    bool fillDriverData(pappl_pr_driver_data_t* driverData, const DriverDataSpec& spec);
}

#endif // BR_THERMAL_DRIVER_DATA_H
