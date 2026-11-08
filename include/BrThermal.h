//
// Created by armin on 23.09.25.
//

#ifndef BR_THERMAL_PAPPL_H
#define BR_THERMAL_PAPPL_H
#include <string>
#include <vector>

extern "C" {
#include <pappl/pappl.h>
}

#include "driver_registry.h"

class BrThermal
{
    public:
        BrThermal();
        void runServer(int argc, char** argv);
    protected:
        static const char* autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *extra);
        static bool driver_cb(pappl_system_t *system, const char *driver_name,const char *device_uri, const char *device_id,pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs, void *thiz);
        static bool driverImpl(const char *driver_name, const char *device_uri, const char *device_id, pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs);

        std::string version{BR_THERMAL_VERSION};
        std::string footer{"br-thermal"};

        std::vector<pappl_pr_driver_t> drivers;
};


#endif //BR_THERMAL_PAPPL_H
