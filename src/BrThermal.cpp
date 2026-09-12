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

#include <cstring>
#include <functional>
#include <iostream>
#include <ostream>
#include <fstream>

extern "C" {
    #include <pappl/pappl.h>
    #include <pappl/base.h>
}


#include <string>

#include "BrThermal.h"

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
    const char *ret = nullptr;
    cups_option_t *did = nullptr;

    const int num_did = papplDeviceParseID(device_id, &did);

    // cupsGetOption returns a null pointer when the key is absent. std::string_view has no
    // null constructor, thus a null pointer here calls strlen(nullptr).
    if (const char *mdl = cupsGetOption("MDL", num_did, did); mdl != nullptr)
    {
        if (const std::string_view model(mdl); !model.empty() && driverMapping.contains(model))
        {
            ret = driverMapping.at(model).c_str();
        }
    }
    cupsFreeOptions(num_did, did);
    return ret;
}

bool BrThermal::driver_cb([[maybe_unused]] pappl_system_t* system, const char* driver_name, const char* device_uri,
    const char* device_id, pappl_pr_driver_data_t* driver_data, [[maybe_unused]] ipp_t** driver_attrs, [[maybe_unused]] void* thiz)
{
    driver_data->format = "image/pwg-raster";

    // device_id is null when the client names the driver, for example an IPP Create-Printer
    // that carries smi55357-driver. papplDeviceParseID then gives no options and
    // cupsGetOption returns a null pointer, which std::string_view cannot hold.
    cups_option_t *did = nullptr;
    const int num_did = papplDeviceParseID(device_id, &did);
    const char *mdl = cupsGetOption("MDL", num_did, did);
    // Own the text: it must stay valid after the options are released.
    std::string model(mdl != nullptr ? mdl : "");
    cupsFreeOptions(num_did, did);

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

    if (std::string_view(driver_name) == "brother_td_2000")
    {
        return drivers::td2000::updateDriverData(driver_data, model);
    }
    if (std::string_view(driver_name) == "brother_pte550w_p750w_p710bt")
    {
        return drivers::pte550w::updateDriverData(driver_data, model);
    }
    return false;
}

