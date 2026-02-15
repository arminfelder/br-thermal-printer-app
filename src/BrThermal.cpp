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

    if (num_did > 0)
    {
        if (const std::string_view model(cupsGetOption("MDL", num_did, did)); !model.empty()){
            if (driverMapping.contains(model))
            {
                ret = driverMapping.at(model).c_str();
            }
        }
    }
    cupsFreeOptions(num_did, did);
    return ret;
}

bool BrThermal::driver_cb([[maybe_unused]] pappl_system_t* system, [[maybe_unused]] const char* driver_name, [[maybe_unused]] const char* device_uri,
    const char* device_id, pappl_pr_driver_data_t* driver_data, [[maybe_unused]] ipp_t** driver_attrs, [[maybe_unused]] void* thiz)
{
    driver_data->format = "image/pwg-raster";

    cups_option_t *did = nullptr;
    const int num_did = papplDeviceParseID(device_id, &did);
    const auto model = std::string_view(cupsGetOption("MDL", num_did, did));

    if (std::strcmp(driver_name, "brother_td_2000") == 0)
    {
        return drivers::td2000::updateDriverData(driver_data, model);
    }

    return false;
}

