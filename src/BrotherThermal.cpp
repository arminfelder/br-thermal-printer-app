//
// Created by armin on 23.09.25.
//

#include <cstring>
#include <functional>
#include <iostream>
#include <ostream>

extern "C" {
    #include <pappl/pappl.h>
}

#include <td2000>

#include "BrotherThermal.h"

// Provide the missing constructor definition
BrotherThermal::BrotherThermal() = default;

// Match the declaration in the header (non-inline, same signature)
void BrotherThermal::runServer(int argc, char **argv)
{
    papplMainloop(argc, argv, version.c_str(), footer.c_str(),
                  drivers.size(), drivers.data(),
                  BrotherThermal::autoadd_cb, BrotherThermal::driver_cb,
                  nullptr, nullptr, nullptr, nullptr, this);
}

// Ensure a return value on all paths
const char* BrotherThermal::autoadd_cb(const char* device_info, const char* device_uri, const char* device_id,
                                       void* thiz)
{
    (void)device_info;
    (void)device_uri;
    auto self = static_cast<BrotherThermal*>(thiz);
    (void)self;

    const char *ret = nullptr;
    cups_option_t *did = nullptr;
    const char *cmd = nullptr;

    const int num_did = papplDeviceParseID(device_id, &did);
    if ((cmd = cupsGetOption("COMMAND SET", num_did, did)) == nullptr)
        cmd = cupsGetOption("CMD", num_did, did);

    if (cmd && strstr(cmd, "PT-CBP"))
    {
        const char *mdl;
        if ((mdl = cupsGetOption("MODEL", num_did, did)) == nullptr)
            mdl = cupsGetOption("MDL", num_did, did);
        if (mdl && self->supportedPrinters.contains(mdl)){
                ret = "brother_td_2000";
        }
    }

    cupsFreeOptions(num_did, did);
    return ret;
}

// Optional stubs to prevent future undefined references; replace with real logic later
bool BrotherThermal::driver_cb(pappl_system_t* system, const char* driver_name, const char* device_uri,
    const char* device_id, pappl_pr_driver_data_t* driver_data, ipp_t** driver_attrs, void* thiz)
{

    driver_data->format = "image/pwg-raster";

    auto self = static_cast<BrotherThermal*>(thiz);
    if (std::strcmp(driver_name, "brother_td_2000") == 0)
    {
        driver_data->printfile_cb = print_cb;
        driver_data->rendjob_cb = rendjob_cb;
        driver_data->rendpage_cb = rendpage_cb;
        driver_data->rstartjob_cb = rstartjob_cb;
        driver_data->rstartpage_cb = rstartpage_cb;
        driver_data->rwriteline_cb = rwriteline_cb;
        driver_data->status_cb = getStatus_cb;

        driver_data->kind = PAPPL_KIND_RECEIPT;

        papplCopyString(driver_data->make_and_model, "Brother TD-2000 Series", sizeof(driver_data->make_and_model));

        driver_data->num_resolution = static_cast<int>(self->resolutions.size());

        for (int i = 0; i < self->resolutions.size(); i++)
        {
            driver_data->x_resolution[i] = self->resolutions[i].x;
            driver_data->y_resolution[i] = self->resolutions[i].y;
        }

        driver_data->x_default = self->resolutions[0].x;
        driver_data->y_default = self->resolutions[0].y;

        driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1;
        driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
        driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;
        driver_data->ppm = 1;

        driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;
        driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;

        driver_data->num_source = 1;
        driver_data->source[0] = "main-roll";

        driver_data->num_type = static_cast<int>(self->media_types.size());

        memcpy(driver_data->type, self->media_types.data(), sizeof(self->media_types.data())*self->media_types.size());

        driver_data->num_media = static_cast<int>(self->media.size());

        memcpy(driver_data->media, self->media.data(), sizeof(self->media.data())*self->media.size());

        if (driver_data->num_media > 0)
        {
            papplCopyString(driver_data->media_ready[0].size_name, driver_data->media[0], sizeof(driver_data->media_ready[0].size_name));

            if (const pwg_media_t *pwg = pwgMediaForPWG(driver_data->media_ready[0].size_name))
            {
                driver_data->media_ready[0].size_width  = pwg->width;
                driver_data->media_ready[0].size_length = pwg->length;
            }

            driver_data->media_ready[0].bottom_margin = 0;
            driver_data->media_ready[0].left_margin   = 0;
            driver_data->media_ready[0].right_margin  = 0;
            driver_data->media_ready[0].top_margin    = 0;

            papplCopyString(driver_data->media_ready[0].source, driver_data->source[0], sizeof(driver_data->media_ready[0].source));
            papplCopyString(driver_data->media_ready[0].type,   driver_data->type[0],   sizeof(driver_data->media_ready[0].type));

            driver_data->media_default = driver_data->media_ready[0];
        }

        return true;
    }

    return false;
}

bool BrotherThermal::getStatus_cb(pappl_printer_t *printer)
{
    pappl_device_t	*device;
    if ((device = papplPrinterOpenDevice(printer)) != nullptr)
    {
        std::vector<uint8_t> buffer{

        };
        papplDeviceWrite(device,)
        std::cout << "Device opened" << std::endl;

    }
    return false;
}

bool BrotherThermal::print_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *)
{
    return false;
}

bool BrotherThermal::rendjob_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *)
{
    return false;
}

bool BrotherThermal::rendpage_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *, unsigned)
{
    return false;
}

bool BrotherThermal::rstartjob_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *)
{
    return false;
}

bool BrotherThermal::rstartpage_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *, unsigned)
{
    return false;
}

bool BrotherThermal::rwriteline_cb(pappl_job_t *, pappl_pr_options_t *, pappl_device_t *, unsigned,
                                   const unsigned char *)
{
    return false;
}

bool BrotherThermal::status_cb(pappl_printer_t *printer)
{


    return false;
}
