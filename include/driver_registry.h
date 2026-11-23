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

// The list of drivers. To add a driver, add one entry here and link its library
// into br_thermal. The autoadd and driver callbacks read only this list.

#ifndef BR_THERMAL_DRIVER_REGISTRY_H
#define BR_THERMAL_DRIVER_REGISTRY_H

#include <array>
#include <string_view>

extern "C" {
#include <pappl/pappl.h>
}

#include "pte550w.h"
#include "td2000.h"

namespace registry
{
    struct DriverEntry
    {
        const char* name;   // PAPPL driver name; it must stay valid for the process lifetime
        const char* info;
        bool (*supportsModel)(std::string_view model);
        bool (*updateDriverData)(pappl_pr_driver_data_t* driverData, const std::string_view& model,
                                 ipp_t** driverAttrs);
    };

    inline constexpr std::array drivers{
        DriverEntry{drivers::td2000::driverName, drivers::td2000::driverInfo,
                    drivers::td2000::supportsModel, drivers::td2000::updateDriverData},
        DriverEntry{drivers::pte550w::driverName, drivers::pte550w::driverInfo,
                    drivers::pte550w::supportsModel, drivers::pte550w::updateDriverData},
    };

    // The driver for an IEEE 1284 MDL, or nullptr.
    inline const DriverEntry* forModel(const std::string_view model)
    {
        for (const auto& entry : drivers)
        {
            if (entry.supportsModel(model))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    // The driver with the PAPPL name `name`, or nullptr.
    inline const DriverEntry* byName(const std::string_view name)
    {
        for (const auto& entry : drivers)
        {
            if (name == entry.name)
            {
                return &entry;
            }
        }
        return nullptr;
    }
}

#endif // BR_THERMAL_DRIVER_REGISTRY_H
