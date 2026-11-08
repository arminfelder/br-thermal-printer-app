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

#include <algorithm>
#include <cups/ipp.h>
#include <cups/pwg.h>
#include <format>
#include <pappl/base.h>
#include <pappl/printer.h>
#include <string>

#include "driver_data.h"
#include "units.h"

namespace brother
{
    namespace
    {
        bool fillMediaCol(pappl_media_col_t& col, const char* sizeName, const Margins& margins,
                          const char* source, const char* type)
        {
            papplCopyString(col.size_name, sizeName, sizeof(col.size_name));
            const pwg_media_t* pwg = pwgMediaForPWG(col.size_name);
            if (pwg == nullptr)
            {
                return false;
            }
            col.size_width    = pwg->width;
            col.size_length   = pwg->length;
            col.bottom_margin = util::units::toPwg(margins.bottom);
            col.left_margin   = util::units::toPwg(margins.left);
            col.right_margin  = util::units::toPwg(margins.right);
            col.top_margin    = util::units::toPwg(margins.top);
            if (source != nullptr)
            {
                papplCopyString(col.source, source, sizeof(col.source));
            }
            papplCopyString(col.type, type, sizeof(col.type));
            return true;
        }
    }

    bool fillDriverData(pappl_pr_driver_data_t* driverData, const DriverDataSpec& spec)
    {
        if (driverData == nullptr || spec.media.empty() || spec.mediaTypes.empty() || spec.sources.empty() ||
            spec.resolutions.empty() || spec.resolutions.size() > PAPPL_MAX_RESOLUTION ||
            spec.media.size() > PAPPL_MAX_MEDIA || spec.mediaTypes.size() > PAPPL_MAX_TYPE ||
            spec.sources.size() > PAPPL_MAX_SOURCE)
        {
            return false;
        }

        const std::string makeAndModel = std::format("Brother {}", spec.model);
        papplCopyString(driverData->make_and_model, makeAndModel.c_str(), sizeof(driverData->make_and_model));

        driverData->output_face_up = true;
        driverData->input_face_up  = true;
        driverData->kind           = spec.kind;
        driverData->ppm            = 1;

        driverData->orient_default  = IPP_ORIENT_PORTRAIT;
        driverData->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->color_default   = PAPPL_COLOR_MODE_MONOCHROME;
        driverData->content_default = PAPPL_CONTENT_TEXT;
        driverData->quality_default = IPP_QUALITY_NORMAL;

        driverData->raster_types      = PAPPL_PWG_RASTER_TYPE_BLACK_1;
        driverData->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;

        driverData->duplex          = PAPPL_DUPLEX_NONE;
        driverData->sides_supported = PAPPL_SIDES_ONE_SIDED;
        driverData->sides_default   = PAPPL_SIDES_ONE_SIDED;
        driverData->finishings      = PAPPL_FINISHINGS_NONE;

        driverData->num_resolution = static_cast<int>(spec.resolutions.size());
        std::ranges::transform(spec.resolutions, driverData->x_resolution, &raster::types::Resolution::x);
        std::ranges::transform(spec.resolutions, driverData->y_resolution, &raster::types::Resolution::y);
        driverData->x_default = spec.resolutions.front().x;
        driverData->y_default = spec.resolutions.front().y;

        driverData->left_right = 0;
        driverData->bottom_top = 0;

        driverData->num_source = static_cast<int>(spec.sources.size());
        std::ranges::copy(spec.sources, driverData->source);

        driverData->num_type = static_cast<int>(spec.mediaTypes.size());
        std::ranges::copy(spec.mediaTypes, driverData->type);

        driverData->num_media = static_cast<int>(spec.media.size());
        std::ranges::copy(spec.media, driverData->media);

        // The ready media must fall in the [roll_min, roll_max] range that PAPPL checks
        // in papplPrinterSetDriverData. Thus the default is a named size with a length,
        // not a zero-length roll_current entry.
        return fillMediaCol(driverData->media_ready[0], spec.media.front(), spec.margins,
                            spec.sources.front(), spec.mediaTypes.front()) &&
               // The default has the source of the ready media: papplPrinterSetReadyMedia
               // then keeps the default equal to the ready media of that source.
               fillMediaCol(driverData->media_default, spec.media.front(), spec.margins,
                            spec.sources.front(), spec.mediaTypes.front());
    }
}
