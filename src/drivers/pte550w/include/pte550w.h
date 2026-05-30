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

#ifndef BR_THERMAL_PTE550W_H
#define BR_THERMAL_PTE550W_H

#include <array>
#include <bit>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#include <pappl/pappl.h>

// ---------------------------------------------------------------------------
// PT-E550W / PT-P750W / PT-P710BT  –  Raster Command Reference v1.02
// https://download.brother.com/welcome/docp100064/cv_pte550wp750wp710bt_eng_raster_102.pdf
//
// Key differences from the TD-2000 driver:
//   • Print head : 128 dots → 16 bytes per raster line (fixed for all models)
//   • Media      : TZe laminated tape only (continuous; no die-cut labels)
//   • Resolution : 180 dpi (E550W, P710BT) or 180/360 dpi (P750W)
//   • Compression: TIFF PackBits is MANDATORY (blank output without it)
//   • Feed margin: 14 dots at 180 dpi, 28 dots at 360 dpi (spec §2.6)
// ---------------------------------------------------------------------------

namespace drivers::pte550w
{
    namespace types
    {
        // The three model variants shipped as one printer family.
        // The P750W is the only one that also supports 360 dpi.
        enum class ModelVariant
        {
            PtE550W,   // 180 dpi only
            PtP750W,   // 180 / 360 dpi
            PtP710BT,  // 180 dpi only (Bluetooth)
        };

        struct Resolution {
            int x;
            int y;
        };

        // Model codes returned in the status response (spec §4, Table 4, byte 4).
        // Values sourced from the raster command reference and cross-checked against
        // open-source implementations of the same protocol.
        enum class ModelCode : uint8_t
        {
            PtE550W  = 0x6E,
            PtP750W  = 0x69,
            PtP710BT = 0x71,
        };

        // Tape type as reported in the status response (spec §4, Table 4, byte 11).
        // These printers only support laminated TZe continuous tape.
        enum class MediaType : uint8_t
        {
            NoMedia          = 0x00,
            LaminatedTape    = 0x01,  // TZe laminated tape (continuous)
            NonLaminatedTape = 0x03,  // TZe non-laminated tape
            HeatShrinkTube   = 0x11,  // HSe heat-shrink tube
            IncompatibleTape = 0xFF,
        };

        enum class CompressionMode : uint8_t
        {
            NoCompression = 0x00,
            // TIFF PackBits — MANDATORY for this printer family;
            // without it the printer outputs blank labels.
            Compression   = 0x02,
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

        enum class NotificationNumber : uint8_t
        {
            NotAvailable             = 0x00,
            CoolingStarted           = 0x03,
            CoolingFinished          = 0x04,
            WaitingForPeeling        = 0x05,
            FinishedWaitingForPeeling = 0x06,
            PrinterPaused            = 0x07,
            FinishedPrinterPaused    = 0x08,
        };

        enum class BatteryLevel : uint8_t
        {
            Full              = 0x00,
            Half              = 0x01,
            Low               = 0x02,
            ChargingRequired  = 0x03,
            AcAdapterInUse    = 0x04,
        };

        enum class CommandMode : uint8_t
        {
            ESCP    = 0x00,
            Raster  = 0x01,
            PTouch  = 0x03,
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

        struct __attribute__((packed)) PrintInfoFields {
            uint8_t  validFields   : 8;
            uint8_t  mediaType     : 8;
            uint8_t  mediaWidth    : 8;
            uint8_t  mediaLength   : 8;
            uint8_t  rasterNumber[4];
            PageType pageType;
            uint8_t  quality       : 8;
        };
        static_assert(sizeof(PrintInfoFields) == 10,
                      "PrintInfo must match protocol size (10 bytes)");
        static_assert(std::is_trivially_copyable_v<PrintInfoFields>);

        // Spec §4 p.26 — PT-E550W/P750W/P710BT error bit assignments.
        struct __attribute__((packed)) ErrorInformation1Fields
        {
            bool    noMedia          : 1;  // bit 0 (01h)
            uint8_t                  : 1;  // bit 1 — not used
            bool    cutterJam        : 1;  // bit 2 (04h)
            bool    weakBatteries    : 1;  // bit 3 (08h)
            uint8_t                  : 1;  // bit 4 — not used
            uint8_t                  : 1;  // bit 5 — not used
            bool    highVoltage      : 1;  // bit 6 (40h)
            uint8_t                  : 1;  // bit 7 — not used
        };
        static_assert(sizeof(ErrorInformation1Fields) == 1);
        static_assert(std::is_trivially_copyable_v<ErrorInformation1Fields>);

        struct __attribute__((packed)) ErrorInformation2Fields
        {
            bool    wrongMedia   : 1;  // bit 0 (01h) — wrong/replace media
            uint8_t              : 1;  // bit 1 — not used
            uint8_t              : 1;  // bit 2 — not used
            uint8_t              : 1;  // bit 3 — not used
            bool    coverOpen    : 1;  // bit 4 (10h)
            bool    overheating  : 1;  // bit 5 (20h)
            uint8_t              : 1;  // bit 6 — not used
            uint8_t              : 1;  // bit 7 — not used
        };
        static_assert(sizeof(ErrorInformation2Fields) == 1);
        static_assert(std::is_trivially_copyable_v<ErrorInformation2Fields>);

        // Spec §4 p.33: ESC i M — bits 0-5 not used, bit 6=auto cut, bit 7=mirror.
        struct __attribute__((packed)) VariousModeSettingsFields
        {
            uint8_t : 6;
            bool autoCut     : 1;  // bit 6
            bool mirrorPrint : 1;  // bit 7
        };
        static_assert(sizeof(VariousModeSettingsFields) == 1);
        static_assert(std::is_trivially_copyable_v<VariousModeSettingsFields>);

        // Spec §4 p.33: ESC i K — Advanced mode settings.
        struct __attribute__((packed)) AdvancedModeSettingsFields
        {
            uint8_t : 2;                  // bits 0-1: not used
            bool halfCut           : 1;   // bit 2: half cut (not PT-P710BT)
            bool noChainPrinting   : 1;   // bit 3: 1=no chain, 0=chain
            bool specialTape       : 1;   // bit 4: special tape (no cutting)
            uint8_t : 1;                  // bit 5: not used
            bool highResolution    : 1;   // bit 6: 1=high res, 0=normal
            bool noBufferClearing  : 1;   // bit 7: no buffer clearing
        };
        static_assert(sizeof(AdvancedModeSettingsFields) == 1);
        static_assert(std::is_trivially_copyable_v<AdvancedModeSettingsFields>);

        template <typename Fields, std::size_t Size>
        struct PackedProtocolBlock {
            static_assert(sizeof(Fields) == Size,
                          "Protocol field size must match raw buffer size");
            static_assert(std::is_trivially_copyable_v<Fields>,
                          "Protocol fields must be trivially copyable");

            std::array<uint8_t, Size> raw{};

            [[nodiscard]] Fields fields() const noexcept {
                return std::bit_cast<Fields>(raw);
            }
            void set_fields(const Fields& value) noexcept {
                raw = std::bit_cast<std::array<uint8_t, Size>>(value);
            }
        };

        using VariousModeSettings  = PackedProtocolBlock<VariousModeSettingsFields, 1>;
        using AdvancedModeSettings = PackedProtocolBlock<AdvancedModeSettingsFields, 1>;

        // Status response packet layout – identical 32-byte format to TD-2000.
        // Spec §4 p.22 (Table 4): byte 0 (printHeadMark) is always 0x80.
        struct __attribute__((packed)) PrinterInfoFields
        {
            uint8_t                  printHeadMark      : 8;
            uint8_t                  size               : 8;
            uint8_t                                     : 8;
            uint8_t                  seriesCode         : 8;
            ModelCode                model;
            uint8_t                                     : 8;
            BatteryLevel             batteryLevel       : 8;
            uint8_t                                     : 8;
            ErrorInformation1Fields  errorInformation1;
            ErrorInformation2Fields  errorInformation2;
            uint8_t                  mediaWidth         : 8;
            MediaType                mediaType;
            uint8_t                                     : 8;
            uint8_t                                     : 8;
            uint8_t                                     : 8;
            uint8_t                  mode               : 8;
            uint8_t                                     : 8;
            uint8_t                  mediaLength        : 8;
            StatusType               statusType;
            Phase                    phaseType          : 8;
            uint8_t                  phaseNumber_lo     : 8;
            uint8_t                  phaseNumber_hi     : 8;
            NotificationNumber       notificationNumber;
            uint8_t                                     : 8;
            uint64_t                                    : 64;
        };
        static_assert(sizeof(PrinterInfoFields) == 32,
                      "PrinterInfoFields must match protocol size (32 bytes)");
        static_assert(std::is_trivially_copyable_v<PrinterInfoFields>);

        using PrinterInfo = PackedProtocolBlock<PrinterInfoFields, 32>;

        struct JobData {
            ModelVariant variant         = ModelVariant::PtP750W;
            bool         useCompression  = true;   // MANDATORY for this family
            int          bytesPerLine    = 16;     // 128 dots / 8 = 16 bytes (fixed)
        };

        struct Margins {
            uint8_t left   = 0;
            uint8_t right  = 0;
            uint8_t top    = 0;
            uint8_t bottom = 0;
        };
    }

    // -----------------------------------------------------------------------
    // Command classes – identical wire format to TD-2000; reproduced here so
    // the pte550w driver is fully self-contained and does not depend on the
    // td2000 library target.
    // -----------------------------------------------------------------------
    namespace commands
    {
        // Raw command byte sequences (spec §4)
        constexpr std::array<uint8_t, 3> status_information_request  {0x1B, 0x69, 0x53};
        constexpr std::array<uint8_t, 3> switch_dynamic_command_mode {0x1B, 0x69, 0x61};
        constexpr std::array<uint8_t, 3> print_information           {0x1B, 0x69, 0x7A};
        constexpr std::array<uint8_t, 3> various_mode_settings       {0x1B, 0x69, 0x4D};
        constexpr std::array<uint8_t, 3> advanced_mode_settings      {0x1B, 0x69, 0x4B};
        constexpr std::array<uint8_t, 3> specify_page_number         {0x1B, 0x69, 0x41};
        constexpr std::array<uint8_t, 3> specify_margin_amount       {0x1B, 0x69, 0x64};
        constexpr std::array<uint8_t, 4> switch_auto_status_notify   {0x1B, 0x69, 0x21, 0x00};
        constexpr std::array<uint8_t, 1> select_compression_mode     {0x4D};
        // Spec §3 p.22: 'G' (47h), NOT 'g' (67h) as used by TD-2000.
        constexpr std::array<uint8_t, 1> raster_graphics_transfer    {0x47};
        constexpr std::array<uint8_t, 1> zero_raster_graphics        {0x5A};
        constexpr std::array<uint8_t, 1> print_cmd                   {0x0C};
        constexpr std::array<uint8_t, 1> print_with_feeding          {0x1A};

        // Spec §2.1 p.5: 100-byte invalidate for PT family (not 200 like TD).
        class Invalidate {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            { return std::vector<uint8_t>(100, 0x00); }
        };

        class Initialize {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            { return {0x1B, 0x40}; }
        };

        class StatusInformationRequest {
        public:
            [[nodiscard]] std::array<uint8_t, 3> get() const noexcept
            { return status_information_request; }
        };

        class SwitchDynamicCommandMode {
        public:
            explicit SwitchDynamicCommandMode(const types::CommandMode mode)
                : commandMode(mode) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(switch_dynamic_command_mode.size() + 1);
                data.insert(data.end(),
                            switch_dynamic_command_mode.begin(),
                            switch_dynamic_command_mode.end());
                data.push_back(static_cast<uint8_t>(commandMode));
                return data;
            }
        private:
            types::CommandMode commandMode;
        };

        class PrintInformation {
        public:
            explicit PrintInformation(const types::PrintInfoFields info)
                : printInfo(info) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(print_information.size() + sizeof(printInfo));
                data.insert(data.end(),
                            print_information.begin(), print_information.end());
                const auto bytes = std::bit_cast<std::array<uint8_t, 10>>(printInfo);
                data.insert(data.end(), bytes.begin(), bytes.end());
                return data;
            }
            [[nodiscard]] types::PrintInfoFields fields() const noexcept { return printInfo; }
        private:
            types::PrintInfoFields printInfo{};
        };

        class VariousModeSettings {
        public:
            explicit VariousModeSettings(const types::VariousModeSettings settings)
                : modeSettings(settings) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(various_mode_settings.size() + modeSettings.raw.size());
                data.insert(data.end(),
                            various_mode_settings.begin(), various_mode_settings.end());
                data.insert(data.end(),
                            modeSettings.raw.begin(), modeSettings.raw.end());
                return data;
            }
        private:
            types::VariousModeSettings modeSettings;
        };

        class AdvancedModeSettings {
        public:
            explicit AdvancedModeSettings(const types::AdvancedModeSettings settings)
                : modeSettings(settings) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(advanced_mode_settings.size() + modeSettings.raw.size());
                data.insert(data.end(),
                            advanced_mode_settings.begin(), advanced_mode_settings.end());
                data.insert(data.end(),
                            modeSettings.raw.begin(), modeSettings.raw.end());
                return data;
            }
        private:
            types::AdvancedModeSettings modeSettings;
        };

        class SpecifyPageNumber {
        public:
            explicit SpecifyPageNumber(const uint8_t pageCount)
                : count(pageCount) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(specify_page_number.size() + 1);
                data.insert(data.end(),
                            specify_page_number.begin(), specify_page_number.end());
                data.push_back(count);
                return data;
            }
        private:
            uint8_t count;
        };

        class SwitchAutoStatusNotify {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                return {switch_auto_status_notify.begin(),
                        switch_auto_status_notify.end()};
            }
        };

        class SpecifyMarginAmount {
        public:
            SpecifyMarginAmount(const int before, const int after)
                : beforeMargin(before), afterMargin(after) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(specify_margin_amount.size() + 2);
                data.insert(data.end(),
                            specify_margin_amount.begin(), specify_margin_amount.end());
                data.push_back(static_cast<uint8_t>(beforeMargin));
                data.push_back(static_cast<uint8_t>(afterMargin));
                return data;
            }
        private:
            int beforeMargin;
            int afterMargin;
        };

        class SelectCompressionMode {
        public:
            explicit SelectCompressionMode(const types::CompressionMode mode)
                : compressionMode(mode) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(select_compression_mode.size() + 1);
                data.insert(data.end(),
                            select_compression_mode.begin(),
                            select_compression_mode.end());
                data.push_back(static_cast<uint8_t>(compressionMode));
                return data;
            }
        private:
            types::CompressionMode compressionMode;
        };

        class RasterGraphicsTransfer {
        public:
            RasterGraphicsTransfer(int byteCount, const std::vector<uint8_t>& data)
                : numberOfBytes(byteCount), raster(data) {}
            [[nodiscard]] std::vector<uint8_t> get() const noexcept {
                std::vector<uint8_t> data;
                data.reserve(raster_graphics_transfer.size() + 2 + raster.size());
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
                data.insert(data.end(),
                            raster_graphics_transfer.begin(),
                            raster_graphics_transfer.end());
                data.push_back(static_cast<uint8_t>(numberOfBytes & 0xFF));
                data.push_back(static_cast<uint8_t>((numberOfBytes >> 8) & 0xFF));
                data.insert(data.end(), raster.begin(), raster.end());
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
                return data;
            }
        private:
            int                   numberOfBytes;
            std::vector<uint8_t>  raster;
        };

        class ZeroRasterGraphics {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            { return {0x5A}; }
        };

        class Print {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            { return {0x0C}; }
        };

        class PrintWithFeeding {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const noexcept
            { return {0x1A}; }
        };
    }

    // -----------------------------------------------------------------------
    // USB identifiers  (spec Appendix, cross-checked with USB ID database)
    // -----------------------------------------------------------------------
    namespace usb
    {
        constexpr unsigned int vendorId = 0x04F9;

        const std::map<types::ModelCode, unsigned int> productId = {
            {types::ModelCode::PtE550W,  0x2060},
            {types::ModelCode::PtP750W,  0x2062},
            {types::ModelCode::PtP710BT, 0x2064},
        };

        constexpr int interfaceNumber = 0;
        constexpr int endpointIn      = 0x81;
        constexpr int endpointOut     = 0x02;
        constexpr int timeout         = 0;
    }

    // -----------------------------------------------------------------------
    // Driver-level configuration tables
    // -----------------------------------------------------------------------

    // All three models share the same 128-dot (16-byte) print head.
    // The P750W supports an optional 360-dpi mode; E550W and P710BT are 180-dpi only.
    inline const std::map<types::ModelVariant, types::Resolution> resolutions {
        {types::ModelVariant::PtE550W,   {180, 180}},
        {types::ModelVariant::PtP750W,   {180, 180}},  // expose 180 dpi as default
        {types::ModelVariant::PtP710BT,  {180, 180}},
    };

    // Spec §2.3.1: "180 dpi high, 360 dpi wide" — height-to-width 1:2.
    inline const std::vector<types::Resolution> extraResolutionsP750W {
        {360, 180},
    };

    // All models: 128 print dots = 16 bytes per raster line (spec §2.3, §4 p.31).
    inline const std::map<types::ModelVariant, uint8_t> lineWidth {
        {types::ModelVariant::PtE550W,   16},
        {types::ModelVariant::PtP750W,   16},
        {types::ModelVariant::PtP710BT,  16},
    };

    // Left/right/top/bottom margins (in dots).
    // These printers use TZe tape with symmetric side margins; top/bottom come from
    // the feed margin setting sent via ESC i d (spec §4 p.30, §2.6).
    inline const std::map<types::ModelVariant, types::Margins> margins {
        {types::ModelVariant::PtE550W,  {0, 0, 0, 0}},
        {types::ModelVariant::PtP750W,  {0, 0, 0, 0}},
        {types::ModelVariant::PtP710BT, {0, 0, 0, 0}},
    };

    // Minimum feed margin in dots.  Spec §2.6: for 180-dpi models the minimum
    // is 14 dots; for a 360-dpi pass it doubles to 28 dots.
    inline const std::map<types::ModelVariant, uint8_t> minFeedMarginDots {
        {types::ModelVariant::PtE550W,   14},
        {types::ModelVariant::PtP750W,   14},
        {types::ModelVariant::PtP710BT,  14},
    };

    inline const std::map<std::string_view, types::ModelVariant> modelVariantMap {
        {"PT-E550W",  types::ModelVariant::PtE550W},
        {"PT-P750W",  types::ModelVariant::PtP750W},
        {"PT-P710BT", types::ModelVariant::PtP710BT},
    };

    // Standard PWG convention: x-dimension = tape width (cross-feed),
    // y-dimension = label length (feed direction).
    // Kate landscape sends x=2400 (24mm tape), y=10000 (100mm label) — matches this.
    constexpr std::array<const char*, 7> defaultMedia {
        "roll_default_24x100mm",
        "roll_min_4x4mm",
        "roll_max_24x1000mm",
        "custom_min_4x4mm",
        "custom_max_24x1000mm",
        "roll_current_12x100mm",
        "roll_current_18x100mm",
    };

    constexpr std::array<const char*, 2> mediaTypes {
        "continuous",        // TZe laminated continuous tape
        "labels-continuous", // alias used by some print stacks
    };

    const std::string driverName {"brother_pte550w_p750w_p710bt"};
    const std::string driverInfo {"Brother PT-E550W/PT-P750W/PT-P710BT"};
    const std::set<std::string_view> supportedPrinters {
        "PT-E550W", "PT-P750W", "PT-P710BT"
    };

    static std::map<std::string, types::PrinterInfo> printerStatus{};
    static std::map<std::string, bool>               printerStatusPolling{};
    static std::mutex                                printerStateMutex{};

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------
    namespace callbacks
    {
        void applyPrinterStatus(pappl_printer_t *printer,
                                const types::PrinterInfo &info,
                                const char *uri);
        bool waitForPrinterResume(pappl_job_t *job, pappl_device_t *device,
                                  pappl_printer_t *printer, const char *uri);
        bool print(pappl_job_t *job, pappl_pr_options_t *options,
                   pappl_device_t *device);
        bool startJob(pappl_job_t *job, pappl_pr_options_t *options,
                      pappl_device_t *device);
        bool startPage(pappl_job_t *job, pappl_pr_options_t *options,
                       pappl_device_t *device, unsigned pageNumber);
        bool writeLine(pappl_job_t *job, pappl_pr_options_t *options,
                       pappl_device_t *device, unsigned pageNumber,
                       const unsigned char *pixels);
        bool endPage(pappl_job_t *job, pappl_pr_options_t *options,
                     pappl_device_t *device, unsigned pageNumber);
        bool endJob(pappl_job_t *job, pappl_pr_options_t *options,
                    pappl_device_t *device);
        bool getStatus(pappl_printer_t *printer);
    }

    bool updateDriverData(pappl_pr_driver_data_t *driverData,
                          const std::string_view &model);
}

#endif // BR_THERMAL_PTE550W_H
