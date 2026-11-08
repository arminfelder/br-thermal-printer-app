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
#include <exception>
#include <string>

extern "C" {
#include <pappl/base.h>
#include <pappl/device.h>
#include <pappl/job.h>
#include <pappl/log.h>
#include <pappl/printer.h>
#include <pappl/system.h>
}

namespace util
{
    // The debug dump goes to the service state directory. Without systemd, it goes to the PAPPL temporary directory.
    std::string debugDumpPath(int jobId);

    // The device belongs to PAPPL. The job device is closed by PAPPL after rendjob_cb,
    // and a status device by papplPrinterCloseDevice. A close here would free memory
    // that PAPPL uses again.
    template<std::ranges::contiguous_range R>
    requires std::same_as<std::ranges::range_value_t<R>, uint8_t>
    bool writeToDevice(const R& data, pappl_device_t* device, const int jobId, bool debug = false)
    {
        if (!device)
        {
            return false;
        }

        const auto size = std::ranges::size(data);
        if (debug)
        {
            const std::string filepath = debugDumpPath(jobId);
            std::ofstream debug_file(filepath, std::ios::app | std::ios::binary);
            if (debug_file.is_open())
            {
                debug_file.write(reinterpret_cast<const char*>(std::ranges::data(data)),
                                 static_cast<long>(size));
            }
            const auto msg = std::format("Dumping data to file: {}", filepath);
            papplLogDevice(msg.c_str(), nullptr);
        }

        if (::papplDeviceWrite(device, std::ranges::data(data), size) != static_cast<ssize_t>(size))
        {
            papplLogDevice("Failed write to device", nullptr);
            return false;
        }
        papplDeviceFlush(device);

        return true;
    }
    
    // The MDL key of an IEEE 1284 device ID. Empty when the ID is null or has no MDL.
    std::string modelFromDeviceId(const char* deviceId);

    // PAPPL is C. An exception that leaves a callback ends the server. Each callback
    // runs inside one of these guards, which turns an exception into `false`.
    void logCallbackFailure(pappl_job_t* job, const char* what) noexcept;
    void logCallbackFailure(pappl_printer_t* printer, const char* what) noexcept;
    void logCallbackFailure(pappl_system_t* system, const char* what) noexcept;

    template<typename Owner, std::invocable F>
    requires std::convertible_to<std::invoke_result_t<F>, bool>
    bool guarded(Owner* owner, F&& body) noexcept
    {
        try
        {
            return static_cast<bool>(body());
        }
        catch (const std::exception& e)
        {
            logCallbackFailure(owner, e.what());
        }
        catch (...)
        {
            logCallbackFailure(owner, "non-standard exception");
        }
        return false;
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

    enum class ReadResult : uint8_t
    {
        Packet,  // a full status block with the 0x80 head mark
        NoData,  // zero-length, short or unknown data; try again later
        Error,   // papplDeviceRead failed; the device is gone
    };

    // The USB read in PAPPL can block for up to 10 s when the printer sends no data.
    template<typename Block>
    requires requires(Block b) { { b.raw.data() } -> std::convertible_to<uint8_t*>; { b.raw.size() }; }
    ReadResult readStatusPacket(pappl_device_t* device, Block& info) noexcept
    {
        constexpr uint8_t printHeadMark = 0x80;
        const auto result = papplDeviceRead(device, info.raw.data(), info.raw.size());
        if (result < 0)
        {
            return ReadResult::Error;
        }
        if (result == static_cast<ssize_t>(info.raw.size()) && info.raw.front() == printHeadMark)
        {
            return ReadResult::Packet;
        }
        return ReadResult::NoData;
    }
}

#endif //BR_THERMAL_UTIL_H