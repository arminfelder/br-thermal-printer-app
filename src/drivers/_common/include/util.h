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
// Created by armin on 05.01.26.
//

#ifndef BR_THERMAL_UTIL_H
#define BR_THERMAL_UTIL_H
#include <fstream>
#include <vector>
#include <ranges>
#include <concepts>
#include <span>

extern "C" {
#include <pappl/base.h>
#include <pappl/device.h>
#include <pappl/log.h>
}

namespace util
{
    template<std::ranges::contiguous_range R>
    requires std::same_as<std::ranges::range_value_t<R>, uint8_t>
    bool writeToDevice(const R& data, pappl_device_t* device, const int jobId, bool debug = false)
    {
        if (!device)
        {
            return false;
        }

        if (debug)
        {
            std::string filepath = "/tmp/td2000-debug-" + std::to_string(jobId) + ".dump";
            std::ofstream debug_file(filepath,
                                     std::ios::app | std::ios::binary);
            if (debug_file.is_open())
            {
                debug_file.write(reinterpret_cast<const char*>(std::ranges::data(data)),
                               static_cast<long>(std::ranges::size(data)));
                debug_file.close();
            }
            const auto msg = std::format("Dumping data to file: {}", filepath);
            papplLogDevice(msg.c_str(), nullptr);
        }


        if (::papplDeviceWrite(device, std::ranges::data(data), std::ranges::size(data)) 
            != static_cast<ssize_t>(std::ranges::size(data)))
        {
            papplLogDevice("Failed write to device", nullptr);
            papplDeviceClose(device);
            return false;
        }
        papplDeviceFlush(device);

        return true;
    }
    
    uint8_t reverseBitOrder(uint8_t value);
    std::vector<uint8_t> mirrorLine(const std::span<const uint8_t> &data);
    std::vector<uint8_t> compressLine(const std::vector<unsigned char>& data);

    // Turns one PAPPL raster line into the mirrored, head-width line the printer expects.
    //
    // `delivered` must be sized from header.cupsBytesPerLine and `deliveredDots` from
    // header.cupsWidth. Do not assume that either equals the print head. PAPPL calculates
    // both from the media geometry, thus narrow media gives a shorter line. A 12 mm tape at
    // 180 dpi is 85 dots against a 16-byte head. If you read the full head width from that
    // buffer, the read is out of bounds.
    //
    // The media is centred below the print head, thus the line is centred in the head and
    // the blank margins are equal on the two sides. Raster spec 2.3.5 gives the margins:
    // a 30 x 30 mm label has 116 blank pins, 216 print area pins and 116 blank pins. If the
    // media is wider than the head, the same number of dots is removed from each side.
    std::vector<uint8_t> buildHeadLine(const std::span<const uint8_t> &delivered,
                                       size_t deliveredDots,
                                       size_t headDots);
}

#endif //BR_THERMAL_UTIL_H