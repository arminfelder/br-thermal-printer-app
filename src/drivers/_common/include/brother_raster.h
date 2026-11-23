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

// The Brother raster command set. The TD-2000 and PT-E550W/P750W/P710BT raster
// references use the same commands and the same 32-byte status block. Only the
// items in this file are common. Model-specific bits stay in each driver.

#ifndef BR_THERMAL_BROTHER_RASTER_H
#define BR_THERMAL_BROTHER_RASTER_H

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace brother::raster
{
    // The protocol blocks are little-endian and std::bit_cast copies host byte order.
    static_assert(std::endian::native == std::endian::little, "Brother raster blocks need a little-endian host");

    namespace types
    {
        enum class CompressionMode : uint8_t
        {
            NoCompression = 0x00,
            Compression   = 0x02,   // TIFF PackBits
        };

        enum class StatusType : uint8_t
        {
            ReplyToStatusRequest = 0x00,
            PrintingCompleted    = 0x01,
            ErrorOccurred        = 0x02,
            ExitIfMode           = 0x03,
            TurnedOff            = 0x04,
            Notification         = 0x05,
            PhaseChange          = 0x06,
        };

        enum class Phase : uint8_t
        {
            ReceivingState = 0x00,
            PrintingState  = 0x01,
        };

        enum class CommandMode : uint8_t
        {
            ESCP   = 0x00,
            Raster = 0x01,
            PTouch = 0x03,
        };

        enum class PageType : uint8_t
        {
            startingPage = 0x00,
            otherPage    = 0x01,
        };

        enum class PrintInfoFlags : uint8_t
        {
            pi_kind    = 0x02,
            pi_width   = 0x04,
            pi_length  = 0x08,
            pi_quality = 0x40,
            pi_recover = 0x80,
        };

        struct Resolution
        {
            int x;
            int y;
        };

        // ESC i z payload. {n10} is fixed at 0 on both families.
        struct __attribute__((packed)) PrintInfoFields
        {
            uint8_t  validFields : 8;
            uint8_t  mediaType   : 8;
            uint8_t  mediaWidth  : 8;
            uint8_t  mediaLength : 8;
            uint8_t  rasterNumber[4];
            PageType pageType;
            uint8_t  quality     : 8;
        };
        static_assert(sizeof(PrintInfoFields) == 10, "PrintInfo must match protocol size (10 bytes)");
        static_assert(std::is_trivially_copyable_v<PrintInfoFields>);

        template <typename Fields, std::size_t Size>
        struct PackedProtocolBlock
        {
            static_assert(sizeof(Fields) == Size, "Protocol field size must match raw buffer size");
            static_assert(std::is_trivially_copyable_v<Fields>, "Protocol fields must be trivially copyable");

            std::array<uint8_t, Size> raw{};

            [[nodiscard]] Fields fields() const noexcept { return std::bit_cast<Fields>(raw); }

            void set_fields(const Fields& value) noexcept
            {
                raw = std::bit_cast<std::array<uint8_t, Size>>(value);
            }

            bool operator==(const PackedProtocolBlock&) const noexcept = default;
        };

        // The 32-byte status block. The layout is the same for all supported families.
        // The error bits and the model, media and notification codes are different, thus
        // the driver supplies those types.
        template <typename ModelCode, typename Error1, typename Error2, typename MediaType,
                  typename NotificationNumber>
        struct __attribute__((packed)) StatusBlockFields
        {
            uint8_t            printHeadMark : 8;   // always 0x80
            uint8_t            size          : 8;
            uint8_t                          : 8;
            uint8_t            seriesCode    : 8;
            ModelCode          model;
            uint8_t                          : 8;
            uint8_t            battery       : 8;   // encoding differs per printer family
            uint8_t                          : 8;
            Error1             errorInformation1;
            Error2             errorInformation2;
            uint8_t            mediaWidth    : 8;
            MediaType          mediaType;
            uint8_t                          : 8;
            uint8_t                          : 8;
            uint8_t                          : 8;
            uint8_t            mode          : 8;
            uint8_t                          : 8;
            uint8_t            mediaLength   : 8;
            StatusType         statusType;
            Phase              phaseType     : 8;
            uint8_t            phaseNumberHigh : 8;   // offset 20, higher order byte
            uint8_t            phaseNumberLow  : 8;   // offset 21, lower order byte
            NotificationNumber notificationNumber;
            uint8_t                          : 8;
            uint64_t                         : 64;
        };

        template <typename ModelCode, typename Error1, typename Error2, typename MediaType,
                  typename NotificationNumber>
        using StatusBlock =
            PackedProtocolBlock<StatusBlockFields<ModelCode, Error1, Error2, MediaType, NotificationNumber>, 32>;
    }

    namespace commands
    {
        constexpr std::array<uint8_t, 2> initialize                  {0x1B, 0x40};
        constexpr std::array<uint8_t, 3> status_information_request  {0x1B, 0x69, 0x53};
        constexpr std::array<uint8_t, 3> switch_dynamic_command_mode {0x1B, 0x69, 0x61};
        constexpr std::array<uint8_t, 3> print_information           {0x1B, 0x69, 0x7A};
        constexpr std::array<uint8_t, 3> specify_margin_amount       {0x1B, 0x69, 0x64};
        constexpr std::array<uint8_t, 1> select_compression_mode     {0x4D};
        constexpr std::array<uint8_t, 1> zero_raster_graphics        {0x5A};
        constexpr std::array<uint8_t, 1> print                       {0x0C};
        constexpr std::array<uint8_t, 1> print_with_feeding          {0x1A};

        // The byte count is per model. See section 2.1 of each raster reference.
        class Invalidate
        {
        public:
            explicit Invalidate(const std::size_t byteCount) : count(byteCount) {}
            [[nodiscard]] std::vector<uint8_t> get() const { return std::vector<uint8_t>(count, 0x00); }
        private:
            std::size_t count;
        };

        class Initialize
        {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const { return {initialize.begin(), initialize.end()}; }
        };

        class StatusInformationRequest
        {
        public:
            [[nodiscard]] std::array<uint8_t, 3> get() const noexcept { return status_information_request; }
        };

        class SwitchDynamicCommandMode
        {
        public:
            explicit SwitchDynamicCommandMode(const types::CommandMode mode) : commandMode(mode) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{switch_dynamic_command_mode.begin(), switch_dynamic_command_mode.end()};
                data.push_back(static_cast<uint8_t>(commandMode));
                return data;
            }
        private:
            types::CommandMode commandMode;
        };

        class PrintInformation
        {
        public:
            explicit PrintInformation(const types::PrintInfoFields info) : printInfo(info) {}
            explicit PrintInformation(const std::array<uint8_t, 10> data)
                : printInfo(std::bit_cast<types::PrintInfoFields>(data)) {}

            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{print_information.begin(), print_information.end()};
                const auto bytes = std::bit_cast<std::array<uint8_t, 10>>(printInfo);
                data.insert(data.end(), bytes.begin(), bytes.end());
                return data;
            }

            [[nodiscard]] types::PrintInfoFields fields() const noexcept { return printInfo; }

        private:
            types::PrintInfoFields printInfo{};
        };

        // ESC i {op} {n}: a one-byte mode block. ESC i M and ESC i K use it.
        template <uint8_t Op, typename Block>
        class ModeSettings
        {
        public:
            explicit ModeSettings(const Block settings) : modeSettings(settings) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{0x1B, 0x69, Op};
                data.insert(data.end(), modeSettings.raw.begin(), modeSettings.raw.end());
                return data;
            }
        private:
            Block modeSettings;
        };

        // ESC i d {n1} {n2}: one margin in dots, n1 + n2 * 256. The printer applies it at
        // both ends of the print area.
        class SpecifyMarginAmount
        {
        public:
            explicit SpecifyMarginAmount(const uint16_t dots) : marginDots(dots) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{specify_margin_amount.begin(), specify_margin_amount.end()};
                data.push_back(static_cast<uint8_t>(marginDots & 0xFFU));
                data.push_back(static_cast<uint8_t>(marginDots >> 8U));
                return data;
            }
        private:
            uint16_t marginDots;
        };

        class SelectCompressionMode
        {
        public:
            explicit SelectCompressionMode(const types::CompressionMode mode) : compressionMode(mode) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{select_compression_mode.begin(), select_compression_mode.end()};
                data.push_back(static_cast<uint8_t>(compressionMode));
                return data;
            }
        private:
            types::CompressionMode compressionMode;
        };

        // The byte count that follows the raster opcode.
        enum class RasterCount : uint8_t
        {
            ZeroPrefixedByte,   // 00h {n}: TD-2000, QL, RJ ('g')
            LittleEndian16,     // {n1} {n2}, n1 + n2 * 256: PT-E550W ('G'), PT-H500 ('g')
        };

        template <uint8_t Opcode, RasterCount Count>
        class RasterTransfer
        {
        public:
            RasterTransfer(const int byteCount, const std::vector<uint8_t>& data) : numberOfBytes(byteCount), raster(data) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data;
                data.reserve(3 + raster.size());
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
                data.push_back(Opcode);
                if constexpr (Count == RasterCount::ZeroPrefixedByte)
                {
                    data.push_back(0x00);
                    data.push_back(static_cast<uint8_t>(numberOfBytes));
                }
                else
                {
                    data.push_back(static_cast<uint8_t>(numberOfBytes & 0xFF));
                    data.push_back(static_cast<uint8_t>((numberOfBytes >> 8) & 0xFF));
                }
                data.insert(data.end(), raster.begin(), raster.end());
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
                return data;
            }
        private:
            int numberOfBytes;
            std::vector<uint8_t> raster;
        };

        class ZeroRasterGraphics
        {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const { return {zero_raster_graphics.begin(), zero_raster_graphics.end()}; }
        };

        class Print
        {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const { return {print.begin(), print.end()}; }
        };

        class PrintWithFeeding
        {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const { return {print_with_feeding.begin(), print_with_feeding.end()}; }
        };
    }
}

#endif // BR_THERMAL_BROTHER_RASTER_H
