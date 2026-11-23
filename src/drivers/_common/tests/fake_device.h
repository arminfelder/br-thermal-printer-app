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

// A PAPPL device scheme with scripted reads for unit tests. Each read reports the
// next entry of `results` as its byte count; -1 is an error, a positive count fills
// the buffer with zeros and `firstByte` at index 0.
//
// A block in `queue` goes first: the read returns it and removes it. `onWrite` can add
// blocks to `queue`, thus a test can make a reply to a command. All bytes that the
// driver writes go to `written`.

#ifndef BR_THERMAL_TEST_FAKE_DEVICE_H
#define BR_THERMAL_TEST_FAKE_DEVICE_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

extern "C" {
#include <pappl/pappl.h>
}

namespace test
{
    struct FakeDeviceScript
    {
        std::vector<ssize_t> results;
        uint8_t firstByte = 0x80;
        // When not empty, a positive read returns these bytes instead of firstByte and zeros.
        std::vector<uint8_t> packet{};
        size_t calls = 0;
        std::deque<std::vector<uint8_t>> queue{};
        std::function<void(std::span<const uint8_t>)> onWrite{};
        std::vector<uint8_t> written{};
    };

    inline FakeDeviceScript* g_script = nullptr;

    inline bool fakeOpen(pappl_device_t*, const char*, const char*) { return true; }
    inline void fakeClose(pappl_device_t*) {}
    inline ssize_t fakeWrite(pappl_device_t*, const void* buffer, const size_t bytes);
    inline pappl_preason_t fakeStatus(pappl_device_t*) { return PAPPL_PREASON_NONE; }
    inline bool fakeList(pappl_device_cb_t, void*, pappl_deverror_cb_t, void*) { return false; }

    inline ssize_t fakeRead(pappl_device_t*, void* buffer, const size_t bytes)
    {
        const auto index = g_script->calls++;
        if (!g_script->queue.empty())
        {
            const auto block = std::move(g_script->queue.front());
            g_script->queue.pop_front();
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            std::span<uint8_t> const out(static_cast<uint8_t*>(buffer), bytes);
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif
            const auto count = std::min(bytes, block.size());
            std::ranges::copy(std::span(block).first(count), out.begin());
            return static_cast<ssize_t>(count);
        }
        const auto result = index < g_script->results.size() ? g_script->results[index] : ssize_t{0};
        if (result > 0)
        {
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            std::span<uint8_t> const out(static_cast<uint8_t*>(buffer), bytes);
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif
            std::ranges::fill(out, uint8_t{0});
            out.front() = g_script->firstByte;
            if (!g_script->packet.empty())
            {
                std::ranges::copy(std::span(g_script->packet).first(std::min(bytes, g_script->packet.size())),
                                  out.begin());
            }
        }
        return result;
    }

    inline ssize_t fakeWrite(pappl_device_t*, const void* buffer, const size_t bytes)
    {
        if (g_script != nullptr)
        {
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
            std::span<const uint8_t> const data(static_cast<const uint8_t*>(buffer), bytes);
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif
            g_script->written.insert(g_script->written.end(), data.begin(), data.end());
            if (g_script->onWrite)
            {
                g_script->onWrite(data);
            }
        }
        return static_cast<ssize_t>(bytes);
    }

    inline constexpr const char* fakeDeviceUri = "fakestatus://unit";

    // Makes the "fakestatus" scheme known to PAPPL and sends its reads and writes to `script`.
    inline void useFakeDevice(FakeDeviceScript& script)
    {
        static bool registered = false;
        if (!registered)
        {
            papplDeviceAddScheme("fakestatus", PAPPL_DEVTYPE_CUSTOM_LOCAL, fakeList, fakeOpen, fakeClose,
                                 fakeRead, fakeWrite, fakeStatus, nullptr);
            registered = true;
        }
        g_script = &script;
    }

    // The caller owns the returned device and must close it with papplDeviceClose.
    inline pappl_device_t* openFakeDevice(FakeDeviceScript& script)
    {
        useFakeDevice(script);
        return papplDeviceOpen(fakeDeviceUri, "test", nullptr, nullptr);
    }

    // A 32-byte status block. Offsets: 8 and 9 error bits, 18 status type, 19 phase type,
    // 22 notification number.
    inline std::vector<uint8_t> statusBlock(const uint8_t statusType, const uint8_t phaseType = 0,
                                            const uint8_t error1 = 0, const uint8_t error2 = 0,
                                            const uint8_t notification = 0)
    {
        std::vector<uint8_t> block(32, 0);
        block[0]  = 0x80;
        block[1]  = 0x20;
        block[8]  = error1;
        block[9]  = error2;
        block[18] = statusType;
        block[19] = phaseType;
        block[22] = notification;
        return block;
    }
}

#endif // BR_THERMAL_TEST_FAKE_DEVICE_H
