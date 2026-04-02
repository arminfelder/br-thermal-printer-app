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

#include <algorithm>
#include <ostream>
#include <fstream>
#include <ranges>

extern "C" {
#include <pappl/base.h>
#include <pappl/pappl.h>
}

#include "util.h"

namespace util
{
    constexpr uint8_t reverseBitOrder(const uint8_t value)
    {
       return static_cast<uint8_t>(
            (value & 0x01u) << 7 |
            (value & 0x02u) << 5 |
            (value & 0x04u) << 3 |
            (value & 0x08u) << 1 |
            (value & 0x10u) >> 1 |
            (value & 0x20u) >> 3 |
            (value & 0x40u) >> 5 |
            (value & 0x80u) >> 7
        );
    }


    std::vector<uint8_t> mirrorLine(const std::span<const uint8_t> &data)
    {
        std::vector<uint8_t> reversed;
        reversed.reserve(data.size());

        std::ranges::transform(
            data | std::views::reverse,
            std::back_inserter(reversed),
            reverseBitOrder
        );

        return reversed;
    }

    std::vector<uint8_t> compressLine(const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> compressed;

        for (size_t i = 0; i < data.size();)
        {
            const auto currentByte = data[i];
            size_t runLength = 1;

            // count sequentially repeating bytes
            while (i + runLength < data.size() && data[i + runLength] == currentByte && runLength < 128)
            {
                runLength++;
            }

            if (runLength >= 2)
            {
                // Run of identical bytes: use RLE
                // For runs of n identical bytes, store (257-n) followed by the byte
                compressed.push_back(static_cast<unsigned char>(257 - runLength));
                compressed.push_back(currentByte);
                i += runLength;
            }
            else
            {
                // Look for literal run (different consecutive bytes)
                size_t literalLength = 1;

                // Find how many non-repeating bytes we have
                while (i + literalLength < data.size() && literalLength < 128)
                {
                    // Check if next byte starts a run of 2 or more
                    if (i + literalLength + 1 < data.size() &&
                        data[i + literalLength] == data[i + literalLength + 1])
                    {
                        break;
                    }
                    literalLength++;
                }

                // Store literal run: length-1 followed by the bytes
                compressed.push_back(static_cast<uint8_t>(literalLength - 1));
                for (size_t j = 0; j < literalLength; j++)
                {
                    compressed.push_back(data[i + j]);
                }
                i += literalLength;
            }
        }
        return compressed;
    }
}
