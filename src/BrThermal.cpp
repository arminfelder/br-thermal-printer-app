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
// Created by armin on 23.09.25.
//

#include "driver_registry.h"
#include <cups/ipp.h>
#include <pappl/mainloop.h>
#include <string_view>

extern "C" {
   #include <pappl/base.h>
}


#include <string>

#include "BrThermal.h"
#include "util.h"

BrThermal::BrThermal()
{
    drivers.reserve(registry::drivers.size());
    for (const auto& entry : registry::drivers)
    {
        drivers.push_back({entry.name, entry.info, nullptr, nullptr});
    }
}

void BrThermal::runServer(const int argc, char** argv)
{
    papplMainloop(argc, argv, version.c_str(), footer.c_str(),
                  static_cast<int>(drivers.size()), drivers.data(),
                  BrThermal::autoadd_cb, BrThermal::driver_cb,
                  nullptr, nullptr, nullptr, nullptr, this);
}

const char* BrThermal::autoadd_cb([[maybe_unused]] const char* device_info, [[maybe_unused]] const char* device_uri, const char* device_id,
                                        [[maybe_unused]] void* extra)
{
    // PAPPL is C: an exception must not leave this callback.
    try
    {
        const auto* entry = registry::forModel(util::modelFromDeviceId(device_id));
        return entry != nullptr ? entry->name : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}

bool BrThermal::driver_cb(pappl_system_t* system, const char* driver_name, const char* device_uri,
    const char* device_id, pappl_pr_driver_data_t* driver_data, ipp_t** driver_attrs, [[maybe_unused]] void* thiz)
{
    return util::guarded(system, [&]
    {
        return driverImpl(driver_name, device_uri, device_id, driver_data, driver_attrs);
    });
}

bool BrThermal::driverImpl(const char* driver_name, const char* device_uri, const char* device_id,
                           pappl_pr_driver_data_t* driver_data, ipp_t** driver_attrs)
{
    if (driver_data == nullptr)
    {
        return false;
    }
    driver_data->format = "image/pwg-raster";

    // device_id is null when the client names the driver, for example an IPP Create-Printer
    // that carries smi55357-driver. The model is then empty.
    std::string model = util::modelFromDeviceId(device_id);

    if (model.empty() && device_uri != nullptr)
    {
        // No device ID, thus take the model from the device URI. PAPPL builds it as
        // "usb://Brother/PT-P750W?serial=000C3G547030", so the model is the last path
        // segment, without the query.
        std::string_view uri(device_uri);
        if (const auto query = uri.find('?'); query != std::string_view::npos)
        {
            uri = uri.substr(0, query);
        }
        if (const auto slash = uri.rfind('/'); slash != std::string_view::npos)
        {
            model = uri.substr(slash + 1);
        }
    }

    if (driver_name == nullptr)
    {
        return false;
    }

    const auto* entry = registry::byName(driver_name);
    return entry != nullptr && entry->updateDriverData(driver_data, model, driver_attrs);
}

