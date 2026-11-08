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
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include <pappl/pappl.h>
#include "brother_raster.h"
#include "driver_data.h"
#include "units.h"

// ---------------------------------------------------------------------------
// PT-E550W / PT-P750W / PT-P710BT  –  Raster Command Reference v1.02
// https://download.brother.com/welcome/docp100064/cv_pte550wp750wp710bt_eng_raster_102.pdf
//
// Key differences from the TD-2000 driver:
//   • Print head : 128 dots → 16 bytes per raster line (fixed for all models)
//   • Media      : TZe laminated tape only (continuous; no die-cut labels)
//   • Resolution : 180x180 dpi. 180x360 dpi exists but is not advertised, see `resolutions`.
//   • Compression: TIFF PackBits is MANDATORY (blank output without it)
//   • Feed margin: 14 dots at 180 dpi, 28 dots at 360 dpi (spec §2.3.3)
// ---------------------------------------------------------------------------

namespace drivers::pte550w
{
    namespace types
    {
        using brother::raster::types::CommandMode;
        using brother::raster::types::CompressionMode;
        using brother::raster::types::PackedProtocolBlock;
        using brother::raster::types::PageType;
        using brother::raster::types::Phase;
        using brother::raster::types::PrintInfoFields;
        using brother::raster::types::PrintInfoFlags;
        using brother::raster::types::Resolution;
        using brother::raster::types::StatusType;
        using Margins = brother::Margins;

        // The three model variants shipped as one printer family.
        enum class ModelVariant
        {
            PtE550W,
            PtP750W,
            PtP710BT,
        };

        // Status block offset 4 (spec §4 p.24). The spec gives no code for the PT-P710BT.
        enum class ModelCode : uint8_t
        {
            PtE550W  = 0x66,   // "f"
            PtP750W  = 0x68,   // "h"
        };

        // Status block offset 22 (spec §4 p.29, table 7). The PT family has no cooling
        // or pause notification.
        enum class NotificationNumber : uint8_t
        {
            NotAvailable = 0x00,
            CoverOpen    = 0x01,
            CoverClosed  = 0x02,
        };

        // Tape type as reported in the status response (spec §4, Table 4, byte 11).
        // These printers only support laminated TZe continuous tape.
        enum class MediaType : uint8_t
        {
            NoMedia          = 0x00,
            LaminatedTape    = 0x01,  // TZe laminated tape (continuous)
            NonLaminatedTape = 0x03,  // TZe non-laminated tape
            HeatShrinkTube   = 0x11,  // HSe heat-shrink tube 2:1
            HeatShrinkTube3To1 = 0x17,
            IncompatibleTape = 0xFF,
        };

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

        using VariousModeSettings  = PackedProtocolBlock<VariousModeSettingsFields, 1>;
        using AdvancedModeSettings = PackedProtocolBlock<AdvancedModeSettingsFields, 1>;

        // There is no full-cut bit. Full is auto cut without half cut.
        enum class CutMode
        {
            Full,
            Half,          // the laminate is cut, the backing is not
            None,
            SpecialTape,   // ESC i K bit 4: tape that must not be cut
        };

        struct ModeBytes
        {
            VariousModeSettings  various;    // ESC i M
            AdvancedModeSettings advanced;   // ESC i K
        };

        // The vendor options of one job.
        struct JobOptions
        {
            CutMode cutMode       = CutMode::Full;
            bool    chainPrinting = false;
            int     marginMm      = 2;   // spec §2.3.3 minimum
            int     cutEvery      = 1;   // ESC i A
        };

        // Spec §4 p.22 (Table 4): the same 32-byte layout as the TD-2000.
        using PrinterInfoFields = brother::raster::types::StatusBlockFields<
            ModelCode, ErrorInformation1Fields, ErrorInformation2Fields, MediaType, NotificationNumber>;
        static_assert(sizeof(PrinterInfoFields) == 32, "PrinterInfoFields must match protocol size (32 bytes)");
        using PrinterInfo = PackedProtocolBlock<PrinterInfoFields, 32>;
    }

    // PAPPL printer reasons (pappl_preason_t bits) for a status block. Error bits count
    // in every status type, not only in "error occurred".
    [[nodiscard]] unsigned statusReasons(const types::PrinterInfo& info);
    // Always false: the PT family has no cooling or pause notification (spec p.29).
    [[nodiscard]] bool pausesPrinting(const types::PrinterInfo& info);
    [[nodiscard]] bool resumesPrinting(const types::PrinterInfo& info);

    namespace commands
    {
        using brother::raster::commands::Initialize;
        using brother::raster::commands::Invalidate;
        using brother::raster::commands::Print;
        using brother::raster::commands::PrintInformation;
        using brother::raster::commands::PrintWithFeeding;
        using brother::raster::commands::SelectCompressionMode;
        using brother::raster::commands::SpecifyMarginAmount;
        using brother::raster::commands::StatusInformationRequest;
        using brother::raster::commands::SwitchDynamicCommandMode;
        using brother::raster::commands::ZeroRasterGraphics;

        // Spec §4 p.33: ESC i M. Bit 6 is auto cut, bit 7 is mirror.
        using VariousModeSettings  = brother::raster::commands::ModeSettings<0x4D, types::VariousModeSettings>;
        // Spec §4 p.33: ESC i K, advanced mode settings.
        using AdvancedModeSettings = brother::raster::commands::ModeSettings<0x4B, types::AdvancedModeSettings>;
        // Spec §3 p.22: 'G' (47h) with a 16-bit count, not 'g' (67h) as on the TD-2000.
        using RasterGraphicsTransfer =
            brother::raster::commands::RasterTransfer<0x47, brother::raster::commands::RasterCount::LittleEndian16>;

        constexpr std::array<uint8_t, 3> specify_page_number       {0x1B, 0x69, 0x41};
        constexpr std::array<uint8_t, 4> switch_auto_status_notify {0x1B, 0x69, 0x21, 0x00};

        class SpecifyPageNumber {
        public:
            explicit SpecifyPageNumber(const uint8_t pageCount) : count(pageCount) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{specify_page_number.begin(), specify_page_number.end()};
                data.push_back(count);
                return data;
            }
        private:
            uint8_t count;
        };

        class SwitchAutoStatusNotify {
        public:
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                return {switch_auto_status_notify.begin(), switch_auto_status_notify.end()};
            }
        };
    }

    // -----------------------------------------------------------------------
    // USB identifiers, spec Appendix "USB specifications"
    // -----------------------------------------------------------------------
    namespace usb
    {
        constexpr unsigned int vendorId = 0x04F9;

        // Keep dist/60-br-thermal.rules in step with this table.
        const std::map<types::ModelVariant, unsigned int> productId = {
            {types::ModelVariant::PtE550W,  0x2060},
            {types::ModelVariant::PtP750W,  0x2062},
            {types::ModelVariant::PtP710BT, 0x20AF},
        };

        constexpr int interfaceNumber = 0;
        constexpr int endpointIn      = 0x81;
        constexpr int endpointOut     = 0x02;
        constexpr int timeout         = 0;
    }

    // -----------------------------------------------------------------------
    // Driver-level configuration tables
    // -----------------------------------------------------------------------

    // Only 180x180 dpi is advertised.
    //
    // The printers also have 180 dpi across x 360 dpi along the feed (spec §2.3.1,
    // ESC i K bit 6). A PT-P750W printed at 360 dpi along the feed on 2026-11-23, and
    // pageSettings has the commands for it. It is not advertised: the image filter of
    // PAPPL (job-filter.c, fit and fill) assumes square pixels, thus a PNG or JPEG prints
    // at half its length. To enable it, add {180, 360} here. PAPPL then uses the last
    // entry for print-quality high and entry size / 2 for normal.
    inline constexpr std::array<types::Resolution, 1> resolutions {{
        {180, 180},
    }};

    // All models: 128 print dots = 16 bytes per raster line (spec §2.3, §4 p.31).
    inline const std::map<types::ModelVariant, uint8_t> lineWidth {
        {types::ModelVariant::PtE550W,   16},
        {types::ModelVariant::PtP750W,   16},
        {types::ModelVariant::PtP710BT,  16},
    };

    // Minimum feed margin at 180 dpi along the feed (spec §2.3.3 p.17). It doubles at
    // 360 dpi. ESC i d carries dots, unlike the PAPPL margins above.
    inline const std::map<types::ModelVariant, util::units::DotCount> minFeedMargin {
        {types::ModelVariant::PtE550W,   14 * util::units::dot},
        {types::ModelVariant::PtP750W,   14 * util::units::dot},
        {types::ModelVariant::PtP710BT,  14 * util::units::dot},
    };

    inline const std::map<std::string_view, types::ModelVariant> modelVariantMap {
        {"PT-E550W",  types::ModelVariant::PtE550W},
        {"PT-P750W",  types::ModelVariant::PtP750W},
        {"PT-P710BT", types::ModelVariant::PtP710BT},
    };

    // Standard PWG convention: x-dimension = tape width (cross-feed),
    // y-dimension = label length (feed direction).
    // Kate landscape sends x=2400 (24mm tape), y=10000 (100mm label) — matches this.
    // The narrowest TZe tape is 3.5 mm (spec §2.3.2, ID 263).
    constexpr std::array<const char*, 7> defaultMedia {
        "roll_default_24x100mm",
        "roll_min_3.5x4.4mm",   // spec §2.3.4: 4.4 mm minimum length
        "roll_max_24x1000mm",
        "custom_min_3.5x4.4mm",
        "custom_max_24x1000mm",
        "roll_current_12x100mm",
        "roll_current_18x100mm",
    };

    constexpr std::array<const char*, 2> mediaTypes {
        "continuous",        // TZe laminated continuous tape
        "labels-continuous", // alias used by some print stacks
    };

    inline constexpr const char* driverName = "brother_pte550w_p750w_p710bt";
    inline constexpr const char* driverInfo = "Brother PT-E550W/PT-P750W/PT-P710BT";

    // Spec §2.3.4 p.18: maximum length 1000 mm.
    inline constexpr uint32_t maxRasterLines180 = 7086;
    inline constexpr uint32_t maxRasterLines360 = 14172;

    // PAPPL keeps the pointers of the option names, thus they have static storage.
    // The options are keywords and ranges. PAPPL does not show a boolean vendor option
    // in the "options" subcommand.
    namespace vendorOptions
    {
        inline constexpr const char* cutMode       = "brother-cut-mode";
        inline constexpr const char* chainPrinting = "brother-chain-printing";
        inline constexpr const char* margin        = "brother-margin";
        inline constexpr const char* cutEvery      = "brother-cut-every";

        inline constexpr std::array<const char*, 4> cutModeValues{"full", "half", "none", "special-tape"};
        inline constexpr std::array<const char*, 2> chainPrintingValues{"off", "on"};

        // Spec §2.3.3 p.17: 2 mm (14 dots) to 127 mm (900 dots) at 180 dpi. The paper
        // table of the printer gives the same limits (BrMinMargin, BrMaxMargin).
        inline constexpr int marginMinMm = 2;
        inline constexpr int marginMaxMm = 127;

        // With auto cut and a margin below this value, the printer feeds and cuts
        // approximately 24.5 mm of tape before the print. The paper table of the printer
        // (nMinNoCutFeed_dot) gives 175 for each TZe tape. The 172 of spec §2.3.3 is
        // the value of the HS 23.6 mm tube only. The driver does not prevent a smaller
        // margin.
        inline constexpr int noPrecutFeedDots = 175;

        // Spec ESC i A: 1 to 99.
        inline constexpr int cutEveryMin = 1;
        inline constexpr int cutEveryMax = 99;
    }

    [[nodiscard]] std::optional<types::CutMode> parseCutMode(std::string_view value) noexcept;
    [[nodiscard]] const char* cutModeName(types::CutMode mode) noexcept;

    // ESC i M and ESC i K for a cut mode. The bit positions agree with the command
    // builder of P-touch Editor Lite in the PT-P750W firmware.
    // ESC i K bit 3 has the opposite sense: 1 is "no chain printing".
    // The PT-P710BT has no half cut (spec ESC i K), thus Half becomes Full there.
    [[nodiscard]] types::ModeBytes buildModeBytes(types::CutMode mode, bool chainPrinting,
                                                  types::ModelVariant variant, bool highResolution) noexcept;

    // The per-page decisions of startPage.
    struct PageSettings
    {
        bool     highResolution;   // ESC i K bit 6
        uint16_t feedMarginDots;   // ESC i d
        uint32_t rasterLines;      // ESC i z {n5..n8}, clamped to the maximum length
        bool     sendCutEvery;     // ESC i A. Spec §3 p.22: the PT-P710BT does not support it.
        uint8_t  cutEvery;         // ESC i A {n1}
    };

    // feedDpi is the job resolution along the feed (HWResolution[1]): 180 or 360.
    // The margin of `options` is millimetres. The dot count doubles at 360 dpi.
    [[nodiscard]] PageSettings pageSettings(types::ModelVariant variant, unsigned feedDpi, unsigned cupsHeight,
                                            const types::JobOptions& options = {});

    // One TZe tape width. Values from spec §2.3.2 and §2.3.5. The paper table of the
    // PT-P750W firmware (wPinOffsetLeft, nPaperWidthRes_dot) gives the same values.
    struct Tape
    {
        uint8_t statusWidth;      // status block offset 10 and ESC i z {n3}; 4 is the 3.5 mm tape
        int     widthPwg;         // PAPPL media width, hundredths of a millimetre
        int     sideMarginDots;   // tape dots that the head cannot print, on each side
    };

    inline constexpr std::array<Tape, 6> tapes{{
        {4, 350, 0},
        {6, 600, 5},
        {9, 900, 7},
        {12, 1200, 7},
        {18, 1800, 8},
        {24, 2400, 21},
    }};

    // The tape for a PAPPL media width, or for a status width code. Empty: not a TZe width.
    [[nodiscard]] std::optional<Tape> tapeForMediaWidth(util::units::MediaSize width) noexcept;
    [[nodiscard]] std::optional<Tape> tapeForStatusWidth(uint8_t statusWidth) noexcept;

    // The side margin of a tape for PAPPL, rounded up to a hundredth of a millimetre.
    [[nodiscard]] util::units::MediaSize sideMargin(const Tape& tape) noexcept;

    // ESC i z {n3}. The 3.5 mm tape is 4 (spec status table (3) (a)).
    [[nodiscard]] uint8_t tapeWidthCode(util::units::MediaSize width) noexcept;

    // ESC i z payload. PI_RECOVER and PI_WIDTH as in the spec sample (§2.2.3 p.11).
    // PI_KIND is not set: with it, a tape type other than {n2} gives an error status.
    [[nodiscard]] types::PrintInfoFields printInformation(unsigned mediaWidthMm, uint32_t rasterLines,
                                                          types::PageType pageType);

    // true when this driver handles the IEEE 1284 MDL `model`.
    inline bool supportsModel(const std::string_view model) { return modelVariantMap.contains(model); }

    // driverAttrs gets the "-supported" and "-default" attributes of the vendor options.
    bool updateDriverData(pappl_pr_driver_data_t *driverData, const std::string_view &model,
                          ipp_t** driverAttrs = nullptr);
}

#endif // BR_THERMAL_PTE550W_H
