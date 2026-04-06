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
// Created by armin on 14.12.25.
//

#ifndef BR_THERMAL_TD2000_H
#define BR_THERMAL_TD2000_H
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#include <pappl/pappl.h>
#include "media_generated.inc"

namespace drivers::td2000
{
    namespace types
    {
        enum class ModelFamily
        {
            Td2x2x = 0x00,
            Td2x3x = 0x01,
        };

        struct Resolution {
            int x;
            int y;
        };

        enum class ModelCode : uint8_t
        {
            Td2020 = 0x33,
            Td2120N = 0x35,
            Td2130N = 0x36,
            Td2030A = 0x44,
            Td2125N = 0x45,
            Td2125Nwb = 0x46,
            Td2135N = 0x47,
            Td2135Nwb = 0x48,
        };

        enum class MediaType : uint8_t
        {
            NoMedia = 0x00,
            ContinuousLengthTape = 0x0A,
            DieCutLabels = 0x0B,  // spec p.29: print command encoding; status response uses 0x4B
        };

        enum class CompressionMode : uint8_t
        {
            NoCompression = 0x00,
            Compression = 0x02,
        };

        enum class StatusType : uint8_t
        {
            ReplyToStatusRequest = 0x00,
            PrintingCompleted = 0x01,
            ErrorOccurred = 0x02,
            ExitIfMode = 0x03,
            TurnedOff = 0x04,
            Notification = 0x05,
            PhaseChange = 0x06
        };

        enum class Phase : uint8_t
        {
            ReceivingState = 0x00,
            PrintingState = 0x01
        };

        enum class NotificationNumber : uint8_t
        {
            NotAvailable = 0x00,
            CoolingStarted = 0x03,
            CoolingFinished = 0x04,
            WaitingForPeeling = 0x05,
            FinishedWaitingForPeeling = 0x06,
            PrinterPaused = 0x07,
            FinishedPrinterPaused = 0x08
        };

        enum class BatteryLevel : uint8_t
        {
            Full = 0x00,
            Half = 0x01,
            Low = 0x02,
            ChargingRequired = 0x03,
            AcAdapterInUse = 0x04,
        };

        enum class ContinuousLengthTape : uint8_t
        {
            _57MM,
            _58MM
        };

        enum class DieCutLabels : uint8_t
        {
            _51x26MM,
            _30x30MM,
            _40x40MM,
            _40x50MM,
            _40x60MM,
            _50x30MM,
            _60x60MM
        };

        enum class CommandMode : uint8_t
        {
            ESCP = 0x00,
            Raster = 0x01,
            PTouch = 0x03
        };

        enum class PageType : uint8_t
        {
            startingPage,
            otherPage
        };

        enum class PrintInfoFlags : uint8_t
        {
            pi_kind = 0x02,
            pi_width = 0x04,
            pi_length = 0x08,
            pi_quality = 0x40,
            pi_recover = 0x80
        };

        struct __attribute__((packed)) PrintInfoFields{
            uint8_t validFields:8;
            uint8_t mediaType:8;
            uint8_t mediaWidth:8;
            uint8_t mediaLength:8;
            uint8_t rasterNumber[4];
            PageType pageType;
            uint8_t :8;
        };

        static_assert(sizeof(PrintInfoFields) == 10, "PrintInfo must match protocol size (10 bytes)");
        static_assert(std::is_trivially_copyable_v<PrintInfoFields>);

        template <typename Fields, std::size_t Size>
        struct PackedProtocolBlock {
            static_assert(sizeof(Fields) == Size, "Protocol field size must match raw buffer size");
            static_assert(std::is_trivially_copyable_v<Fields>, "Protocol fields must be trivially copyable");

            std::array<uint8_t, Size> raw{};

            [[nodiscard]] Fields fields() const noexcept {
                return std::bit_cast<Fields>(raw);
            }

            void set_fields(const Fields& value) noexcept {
                raw = std::bit_cast<std::array<uint8_t, Size>>(value);
            }
        };

        //TODO: refactor to prevent undefined behaviour under C++
        struct __attribute__((packed)) ErrorInformation1Fields
        {
            bool noMedia : 1;
            bool endOfMedia : 1;
            uint8_t  : 1;
            uint8_t  : 1;
            bool printerInUse:1;
            uint8_t  : 1;
            uint8_t  : 1;
            uint8_t  : 1;
        };

        static_assert(sizeof(ErrorInformation1Fields) == 1, "ErrorInformation1 must match protocol size (1 bytes)");
        static_assert(std::is_trivially_copyable_v<ErrorInformation1Fields>);

        struct __attribute__((packed)) ErrorInformation2Fields
        {
            bool replacingMedia : 1;
            uint8_t  : 1;
            bool communicationError : 1;
            uint8_t  : 1;
            bool coverOpen : 1;
            uint8_t  : 1;
            bool mediaCannotBeFed : 1;
            bool systemError : 1;
        };

        static_assert(sizeof(ErrorInformation2Fields) == 1, "ErrorInformation2 must match protocol size (1 bytes)");
        static_assert(std::is_trivially_copyable_v<ErrorInformation2Fields>);

        struct __attribute__((packed)) VariousModeSettingsFields
        {
            uint8_t :3;
            bool rotate180:1;
            bool peeler:1;
            uint8_t :3;
        };

        static_assert(sizeof(VariousModeSettingsFields) == 1, "VariousModeSettingsFields must match protocol size (1 bytes)");
        static_assert(std::is_trivially_copyable_v<VariousModeSettingsFields>);

        using VariousModeSettings = PackedProtocolBlock<VariousModeSettingsFields, 1>;

        struct __attribute__((packed)) PrinterInfoFields
        {
            uint8_t printHeadMark : 8;
            uint8_t size : 8;
            uint8_t  : 8;
            uint8_t seriesCode : 8;
            ModelCode model;
            uint8_t  : 8;
            BatteryLevel batteryLevel : 8;
            uint8_t  : 8;
            ErrorInformation1Fields errorInformation1;
            ErrorInformation2Fields errorInformation2;
            uint8_t mediaWidth : 8;
            MediaType mediaType;
            uint8_t  : 8;
            uint8_t  : 8;
            uint8_t  : 8;
            uint8_t mode : 8;
            uint8_t  : 8;
            uint8_t mediaLength : 8;
            StatusType statusType;
            Phase phaseType : 8;
            uint8_t phaseNumber_lo : 8;
            uint8_t phaseNumber_hi : 8;
            NotificationNumber notificationNumber;
            uint8_t  : 8;
            uint64_t  : 64;
        };

        static_assert(sizeof(PrinterInfoFields) == 32, "PrinterInfoFields must match protocol size (32 bytes)");
        static_assert(std::is_trivially_copyable_v<PrinterInfoFields>);

        using PrinterInfo = PackedProtocolBlock<PrinterInfoFields, 32>;

        // 127-byte media blob payload of ESC i U (1Bh 69h 55h 77h 01h).
        // Field names are derived from PTD source files (e.g. bst202ed.txt).
        struct __attribute__((packed)) MediaInfoFields {
            uint8_t  sensorId;              // [0]    PTD: nSensorID (63 for TD-2000 series)
            uint8_t  energyRank;            // [1]    PTD: byEnergyRank
            uint8_t  paperWidth;            // [2]    PTD: nPaperWidth/10 (mm)
            uint8_t  paperLengthMm;         // [3]    PTD: nPaperLength/10 (mm); 0 for continuous tape
            uint8_t  headDivide;            // [4]    PTD: byHeadDivide (always 0)
            uint8_t  rollWidMm;             // [5]    PTD: byRollWidMm
            uint8_t  pinOffsetLeft;         // [6]    PTD: wPinOffsetLeft (0–116; unsigned)
            uint8_t  reserved0;             // [7]    reserved (always 0)
            uint16_t imageAreaWidthRes;     // [8-9]  PTD: nImageAreaWidthRes, little-endian (dots)
            uint16_t imageAreaLengthRes;    // [10-11] PTD: nImageAreaLengthRes, little-endian (dots); 0 for continuous
            uint8_t  dieStartPlus;          // [12]   PTD: nDieStartPlus (always 0)
            uint8_t  dieStartRev;           // [13]   PTD: nDieStartRev (always 0)
            uint8_t  dieStartFwd;           // [14]   PTD: nDieStartFwd (always 0)
            uint8_t  virtualOffsetX;        // [15]   PTD: nVirtualOffsetX (dots)
            uint8_t  reserved1;             // [16]   reserved (always 0)
            uint8_t  virtualOffsetY;        // [17]   PTD: nVirtualOffsetY (dots)
            uint8_t  reserved2;             // [18]   reserved (always 0)
            uint8_t  afterFeedPlus;         // [19]   PTD: nAfterFeedPlus (dots)
            uint8_t  reserved3;             // [20]   reserved (always 0)
            uint16_t paperSize;             // [21-22] PTD: wPafMediaID / nPaperSize, little-endian
            // [23-76] PTD capability fields not serialised into the blob (always 0):
            //   dwPaperAbility01 (4 bytes), byReserved001[16], nCompatiblePaperSizes[4], etc.
            uint8_t  reserved4[54];
            char     sizeMM[16];            // [77-92]  PTD: szSizeMM (null-terminated ASCII, e.g. "62 x 29 mm")
            char     sizeIN[16];            // [93-108] PTD: szSizeIN (null-terminated ASCII, e.g. "2.44\" x 1.14\"")
            uint8_t  reserved5[2];          // [109-110] reserved (always 0)
            uint16_t lblPitchDot;           // [111-112] PTD: lblPitchDot, little-endian; 0 for continuous tape
            uint8_t  reserved6[2];          // [113-114] reserved (always 0)
            // [115-123] PTD: reserved_12_[0..8] (first 9 of 12 values).
            // Byte [6] of this array (blob[121]): 0=continuous tape, 1=die-cut label.
            uint8_t  reserved_12_[9];
            int8_t   detectionSensitivity;  // [124] PTD: markNoChkValu (always 0)
            int8_t   luminesenceSensitivity;// [125] always 0
            uint8_t  reserved7;             // [126] reserved (always 0)
        };
        static_assert(sizeof(MediaInfoFields) == 127, "MediaInfo must match protocol size (127 bytes)");
        static_assert(std::is_trivially_copyable_v<MediaInfoFields>);

        struct MediaInfo {
            std::array<uint8_t, 127> raw{};

            [[nodiscard]] MediaInfoFields fields() const noexcept {
                return std::bit_cast<MediaInfoFields>(raw);
            }

            void set_fields(const MediaInfoFields& fields) noexcept {
                raw = std::bit_cast<std::array<uint8_t, 127>>(fields);
            }

            bool operator==(const MediaInfo& expr_lhs)const
            {
                return std::equal(raw.begin(), raw.end(), expr_lhs.raw.begin() );
            }
        };

        struct JobData {
            ModelFamily modelFamily = ModelFamily::Td2x2x;
            bool useCompression = false;
            int bytesPerLine = 56;
        };

        struct Margins {
            uint8_t left = 0;
            uint8_t right = 0;
            uint8_t top = 0;
            uint8_t bottom = 0;
        };

    }

    namespace commands
    {
        constexpr std::array<uint8_t,1> invalidate{0x00};
        constexpr std::array<uint8_t,2> initialize{0x1B,0x40};
        constexpr std::array<uint8_t,3> status_information_request{0x1B,0x69,0x53};
        constexpr std::array<uint8_t,3> switch_dynamic_command_mode{0x1B,0x69,0x61};
        constexpr std::array<uint8_t,5> additional_media_information{0x1B,0x69,0x55,0x77,0x01};
        constexpr std::array<uint8_t,3> print_information{0x1B,0x69,0x7A};
        constexpr std::array<uint8_t,3> various_mode_settings{0x1B,0x69,0x4D};
        constexpr std::array<uint8_t,3> specify_margin_amount{0x1B,0x69,0x64};
        constexpr std::array<uint8_t,1> select_compression_mode{0x4D};
        constexpr std::array<uint8_t,1> raster_graphics_transfer{0x67};
        constexpr std::array<uint8_t,1> zero_raster_graphics{0x5A};
        constexpr std::array<uint8_t,1> print{0x0C};
        constexpr std::array<uint8_t,1> print_with_feeding{0x1A};

        class Invalidate
        {
        public:
            Invalidate() = default;
            ~Invalidate() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept { return invalidate; }
        private:
            std::vector<uint8_t> invalidate{0x00};
        };

        class Initialize
        {
            public:
            Initialize() = default;
            ~Initialize() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept { return initialize; }
        private:
            std::vector<uint8_t> initialize{0x1B, 0x40};
        };

        class StatusInformationRequest
        {
        public:
            StatusInformationRequest() = default;
            ~StatusInformationRequest() = default;
            [[nodiscard]] std::array<uint8_t,3> get() const noexcept { return status_information_request; }

        };

        class SwitchDynamicCommandMode
        {
        public:
            explicit SwitchDynamicCommandMode(const types::CommandMode mode) : commandMode(mode)
            {
            }
            ~SwitchDynamicCommandMode() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve( switch_dynamic_command_mode.size() + 1);
                data.insert(data.end(), switch_dynamic_command_mode.begin(), switch_dynamic_command_mode.end());
                data.push_back(static_cast<uint8_t>(commandMode));
                return data;
            }
        private:
            types::CommandMode commandMode;
        };

        class AdditionalMediaInformation
        {
        public:
            explicit AdditionalMediaInformation(const types::MediaInfo& info) : mediaInformation(info)
            {
            }
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(additional_media_information.size() + mediaInformation.raw.size());

                data.insert(data.end(), additional_media_information.begin(), additional_media_information.end());
                data.insert(data.end(), mediaInformation.raw.begin(), mediaInformation.raw.end());

                return data;
            }
        private:
            types::MediaInfo mediaInformation;
        };

        class PrintInformation
        {
        public:
            explicit PrintInformation(const types::PrintInfoFields info): printInfo(info) {}

            explicit PrintInformation(const std::array<uint8_t, 10> data)
            {
                printInfo = std::bit_cast<types::PrintInfoFields>(data);
            }

            ~PrintInformation() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(print_information.size()+sizeof(printInfo));
                data.insert(data.end(), print_information.begin(), print_information.end());
                const auto infoBytes = std::bit_cast<std::array<uint8_t, 10>>(printInfo);
                data.insert(data.end(), infoBytes.begin(), infoBytes.end());
                return data;
            }

            [[nodiscard]] types::PrintInfoFields fields() const noexcept { return printInfo; }

        private:
            types::PrintInfoFields printInfo{};
        };

        class ErrorInformation1
        {
        public:
            explicit ErrorInformation1(const types::ErrorInformation1Fields info): errorInfo(info) {}
            explicit ErrorInformation1(const std::array<uint8_t, 1> data)
            {
                errorInfo = std::bit_cast<types::ErrorInformation1Fields>(data);
            }

            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.push_back(std::bit_cast<uint8_t>(errorInfo));
                return data;
            }

            [[nodiscard]] types::ErrorInformation1Fields fields() const noexcept { return errorInfo; }

        private:
            types::ErrorInformation1Fields errorInfo{};
        };

        class ErrorInformation2
        {
        public:
            explicit ErrorInformation2(const types::ErrorInformation2Fields info): errorInfo(info) {}
            explicit ErrorInformation2(const std::array<uint8_t, 1> data)
            {
                errorInfo = std::bit_cast<types::ErrorInformation2Fields>(data);
            }

            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.push_back(std::bit_cast<uint8_t>(errorInfo));
                return data;
            }

            [[nodiscard]] types::ErrorInformation2Fields fields() const noexcept { return errorInfo; }

        private:
            types::ErrorInformation2Fields errorInfo{};
        };

        class VariousModeSettings
        {
        public:
            explicit VariousModeSettings(const types::VariousModeSettings settings): modeSettings(settings)
            {

            }
            ~VariousModeSettings() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                const size_t total_size = various_mode_settings.size() + modeSettings.raw.size();
                std::vector<uint8_t> data;
                data.reserve(total_size);
                data.insert(data.end(), various_mode_settings.begin(), various_mode_settings.end());
                data.insert(data.end(), modeSettings.raw.begin(), modeSettings.raw.end());
                return data;
            }

        private:
            types::VariousModeSettings modeSettings;
        };

        class SpecifyMarginAmount
        {
        public:
            SpecifyMarginAmount(const int before, const int after): beforeMargin(before), afterMargin(after)
            {

            }
            ~SpecifyMarginAmount() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(specify_margin_amount.size()+2);
                data.insert(data.end(), specify_margin_amount.begin(), specify_margin_amount.end());
                data.push_back(static_cast<uint8_t>(beforeMargin));
                data.push_back(static_cast<uint8_t>(afterMargin));

                return data;
            }

        private:
            int beforeMargin;
            int afterMargin;
        };

        class SelectCompressionMode
        {
        public:
            explicit SelectCompressionMode(const types::CompressionMode mode) : compressionMode(mode)
            {
            }
            ~SelectCompressionMode() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(select_compression_mode.size());
                data.insert(data.end(), select_compression_mode.begin(), select_compression_mode.end());
                data.push_back(static_cast<uint8_t>(compressionMode));

                return data;
            }

        private:
            types::CompressionMode compressionMode;
        };

        class RasterGraphicsTransfer
        {
        public:
            RasterGraphicsTransfer(int byteCount, const std::vector<uint8_t>& data): numberOfBytes(byteCount), raster(data)
            {

            }
            ~RasterGraphicsTransfer() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(raster_graphics_transfer.size()+2+static_cast<size_t>(numberOfBytes));
                data.insert(data.end(), raster_graphics_transfer.begin(), raster_graphics_transfer.end());
                data.push_back(0);
                data.push_back(static_cast<uint8_t>(numberOfBytes));
                data.insert(data.end(), raster.begin(), raster.end());

                return data;
            }

        private:
            int numberOfBytes;
            std::vector<uint8_t> raster;
        };

        class ZeroRasterGraphics
        {
        public:
            ZeroRasterGraphics() = default;
            ~ZeroRasterGraphics() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(zero_raster_graphics.size());
                data.insert(data.end(), zero_raster_graphics.begin(), zero_raster_graphics.end());
                return data;
            }

        };

        class Print
        {
        public:
            Print() = default;
            ~Print() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(print.size());
                data.insert(data.end(), print.begin(), print.end());
                return data;
            }

        };

        class PrintWithFeeding
        {
        public:
            PrintWithFeeding() = default;
            ~PrintWithFeeding() = default;
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            {
                std::vector<uint8_t> data;
                data.reserve(print_with_feeding.size());
                data.insert(data.end(), print_with_feeding.begin(), print_with_feeding.end());
                return data;
            }

        };
    }


    namespace usb
    {
        constexpr unsigned int vendorId = 0x04F9;

        const std::map<types::ModelCode, unsigned int> productId = {
            {types::ModelCode::Td2020, 0x2055},
            {types::ModelCode::Td2120N, 0x2057},
            {types::ModelCode::Td2130N, 0x2058},
            {types::ModelCode::Td2030A, 0x20F6},
            {types::ModelCode::Td2125N, 0x20F7},
            {types::ModelCode::Td2125Nwb, 0x20F8},
            {types::ModelCode::Td2135N, 0x20F9},
            {types::ModelCode::Td2135Nwb, 0x20FA},
        };

        constexpr int interfaceNumber = 0;
        constexpr int endpointIn = 0x81;
        constexpr int endpointOut = 0x02;
        constexpr int timeout = 0;
    }

    namespace media
    {
        types::MediaInfo getMediaInfoForMedia(types::ModelFamily family, int width, int length, types::MediaType type ) noexcept;

        constexpr types::MediaInfo none{
            .raw =  {}
        };



#define DEFINE_MEDIA_INFO(ns, name) \
    constexpr inline types::MediaInfo name = { .raw = blobs::ns::name }

        namespace td2x2x
        {
            DEFINE_MEDIA_INFO(td2x2x, _58mm);
            DEFINE_MEDIA_INFO(td2x2x, _57mm);
            DEFINE_MEDIA_INFO(td2x2x, _60x60mm);
            DEFINE_MEDIA_INFO(td2x2x, _51x26mm);
            DEFINE_MEDIA_INFO(td2x2x, _50x30mm);
            DEFINE_MEDIA_INFO(td2x2x, _40x60mm);
            DEFINE_MEDIA_INFO(td2x2x, _40x50mm);
            DEFINE_MEDIA_INFO(td2x2x, _40x40mm);
            DEFINE_MEDIA_INFO(td2x2x, _30x30mm);
        }
        namespace td2x3x
        {
            DEFINE_MEDIA_INFO(td2x3x, _58mm);
            DEFINE_MEDIA_INFO(td2x3x, _57mm);
            DEFINE_MEDIA_INFO(td2x3x, _60x60mm);
            DEFINE_MEDIA_INFO(td2x3x, _51x26mm);
            DEFINE_MEDIA_INFO(td2x3x, _50x30mm);
            DEFINE_MEDIA_INFO(td2x3x, _40x60mm);
            DEFINE_MEDIA_INFO(td2x3x, _40x50mm);
            DEFINE_MEDIA_INFO(td2x3x, _40x40mm);
            DEFINE_MEDIA_INFO(td2x3x, _30x30mm);
        }

#undef DEFINE_MEDIA_INFO

    }

    namespace callbacks
    {
        void applyPrinterStatus(pappl_printer_t *printer, const types::PrinterInfo &info, const char *uri);
        bool waitForPrinterResume(pappl_job_t *job, pappl_device_t *device, pappl_printer_t *printer, const char *uri);
        bool print(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        bool startJob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        bool startPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned pageNumber);
        bool writeLine(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned pageNumber, const unsigned char *pixels);
        bool endPage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned pageNumber);
        bool endJob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device);
        bool getStatus(pappl_printer_t *printer);
    }

    bool updateDriverData(pappl_pr_driver_data_t* driverData, const std::string_view& model);

    inline const std::map<std::string_view, types::ModelFamily> modelFamilyMap = {
        {"TD-2020", types::ModelFamily::Td2x2x},
        {"TD-2120N", types::ModelFamily::Td2x2x},
        {"TD-2130N", types::ModelFamily::Td2x3x},
        {"TD-2030A", types::ModelFamily::Td2x3x},
        {"TD-2125N", types::ModelFamily::Td2x2x},
        {"TD-2125NWB", types::ModelFamily::Td2x2x},
        {"TD-2135N", types::ModelFamily::Td2x3x},
        {"TD-2135NWB", types::ModelFamily::Td2x3x}
    };

    inline const std::map<types::ModelFamily, types::Resolution> resolutions{
        {types::ModelFamily::Td2x2x, {203, 203}},
        {types::ModelFamily::Td2x3x, {300, 300}},
    };

    inline const std::map<types::ModelFamily, uint8_t> lineWidth{
        {types::ModelFamily::Td2x2x, 56},
        {types::ModelFamily::Td2x3x, 84},
    };

    inline const std::map<types::ModelFamily, types::Margins> margins{
        {types::ModelFamily::Td2x2x, {
            3,
            3,
            0,
            0
        }},
        {types::ModelFamily::Td2x3x, {
            3,
            3,
            0,
            0
        }}
    };

    // Minimum feed margin in dots for continuous tape per spec §2.6; die-cut must be 0
    inline const std::map<types::ModelFamily, uint8_t> minFeedMarginDots{
        {types::ModelFamily::Td2x2x, 24},  // 203 dpi
        {types::ModelFamily::Td2x3x, 35},  // 300 dpi
    };

    constexpr std::array<const char*,12> defaultMedia{
        // Named roll sizes (non-zero length → visible in print dialogs)
        "roll_default_58x100mm",
        // Size range for continuous roll media (consumed by PAPPL for media-col-database range,
        // not emitted as individual named sizes).
        // Spec §2.3.4: min length 12 mm, max length 1000 mm.
        "roll_min_57x12mm",
        "roll_max_58x1000mm",
        "custom_min_57x12mm",
        "custom_max_58x1000mm",
        // Die-cut label sizes.  Must use a valid PWG mm-class ("om" = Other Metric);
        // "labels" is not a valid class and causes pwgMediaForPWG() to return NULL,
        // making the entry invisible in media-col-database.
        // Sizes per spec §2.3.2 (b) Die-cut labels, IDs 422/431/432/433/434/435/437.
        "om_51x26mm_51x26mm",
        "om_30x30mm_30x30mm",
        "om_40x40mm_40x40mm",
        "om_40x50mm_40x50mm",
        "om_40x60mm_40x60mm",
        "om_50x30mm_50x30mm",
        "om_60x60mm_60x60mm",
    };

    constexpr std::array<const char*,3> mediaTypes{
        "continuous",       // continuous roll tape   → MediaType::ContinuousLengthTape
        "labels",           // pre-cut die-cut labels → MediaType::DieCutLabels
        "labels-continuous" // continuous label stock → MediaType::ContinuousLengthTape
    };

    const std::string driverName { "brother_td_2000"};
    const std::string driverInfo {"Brother TD-2020/2120N/2130N/2030A/2125N/2125NWB/2135N/2135NWB"};
    const std::set<std::string_view> supportedPrinters {
        "TD-2020","TD-2120N","TD-2130N","TD-2030A","TD-2125N","TD-2125NWB","TD-2135N","TD-2135NWB"
    };
    
    static std::map<std::string, types::PrinterInfo> printerStatus{};
    // true while a print job is active: printer pushes status automatically,
    // so getStatus must not send ESC i S (spec §4, ESC i S note).
    static std::map<std::string, bool> printerStatusPolling{};
    static std::mutex printerStateMutex{};

}

#endif //BR_THERMAL_TD2000_H
