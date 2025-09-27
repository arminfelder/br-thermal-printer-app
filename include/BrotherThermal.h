//
// Created by armin on 23.09.25.
//

#ifndef PAPPL_TD2000_BROTHERTHERMAL_H
#define PAPPL_TD2000_BROTHERTHERMAL_H
#include <set>
#include <string>
#include <vector>
extern "C" {
#include <pappl/pappl.h>
}

class BrotherThermal
{
    public:
        BrotherThermal();
        void runServer(int argc, char** argv);
    protected:
        static const char* autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *thiz);
        static bool driver_cb(pappl_system_t *system, const char *driver_name,const char *device_uri, const char *device_id,pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs, void *thiz);
        static bool getStatus_cb(pappl_printer_t *printer);
        static bool print_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        static bool rendjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        static bool rendpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
        static bool rstartjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        static bool rstartpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page);
        static bool rwriteline_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *line);
        static bool status_cb(pappl_printer_t *printer);

        std::string version{"0.1"};
        std::string footer{"placeholder"};

        std::vector<pappl_pr_driver_t> drivers{
                    {"brother_td_2000", "Brother TD-2020/2120N/2130N/2030A/2125N/2125NWB/2135N/2135NWB" , nullptr , nullptr},
        };

        const std::set<std::string> supportedPrinters {
            "TD-2020","TD-2120N","TD-2130N","TD-2030A","TD-2125N","TD-2125NW","TD-2135N","TD-2135NW"
        };

        struct Resolution {
            int x;
            int y;
        };
        const std::vector<Resolution> resolutions{
                    {.x = 203, .y = 203}
        };

        const std::vector<const char*> media{
            "continuous-short_bdl_57x0mm",
            "continuous-short_bdl_58x0mm",
            "labels_bde_51x26mm",
            "labels_bde_30x20mm",
            "labels_bde_40x40mm",
            "labels_bde_40x50mm",
            "labels_bde_40x60mm",
            "labels_bde_50x30mm",
            "labels_bde_60x60mm",
        };

        const std::vector<const char*> media_types{
            "continuous-short",
            "labels"
        };
};


#endif //PAPPL_TD2000_BROTHERTHERMAL_H
