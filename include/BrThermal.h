//
// Created by armin on 23.09.25.
//

#ifndef BR_THERMAL_PAPPL_H
#define BR_THERMAL_PAPPL_H
#include <set>
#include <string>
#include <vector>

#include "td2000.h"
#include "pte550w.h"

extern "C" {
#include <pappl/pappl.h>
}
const std::map<std::string_view,std::string>driverMapping{
                {
                    "TD-2020", drivers::td2000::driverName
                },{
                    "TD-2120N",drivers::td2000::driverName
                },{
                    "TD-2130N",drivers::td2000::driverName
                },{
                    "TD-2030A",drivers::td2000::driverName
                },{
                    "TD-2125N",drivers::td2000::driverName
                },{
                    "TD-2125NW",drivers::td2000::driverName
                },{
                    "TD-2135N",drivers::td2000::driverName
                },{
                    "TD-2135NW",drivers::td2000::driverName
                },{
                    "PT-E550W", drivers::pte550w::driverName
                },{
                    "PT-P750W", drivers::pte550w::driverName
                },{
                    "PT-P710BT",drivers::pte550w::driverName
                }
};

class BrThermal
{
    public:
        BrThermal() = default;
        void runServer(int argc, char** argv);
    protected:
        static const char* autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *extra);
        static bool driver_cb(pappl_system_t *system, const char *driver_name,const char *device_uri, const char *device_id,pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs, void *thiz);

        std::string version{BR_THERMAL_VERSION};
        std::string footer{"br-thermal"};

        std::vector<pappl_pr_driver_t> drivers{
            {drivers::td2000::driverName.c_str(), drivers::td2000::driverInfo.c_str(), nullptr, nullptr},
            {drivers::pte550w::driverName.c_str(), drivers::pte550w::driverInfo.c_str(), nullptr, nullptr},
        };
};


#endif //BR_THERMAL_PAPPL_H
