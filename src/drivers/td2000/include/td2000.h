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
#include <string>
#include <type_traits>
#include <vector>
#include <pappl/pappl.h>
#include "media_generated.inc"
#include "brother_raster.h"
#include "driver_data.h"
#include "units.h"

namespace drivers::td2000
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

        enum class ModelFamily
        {
            Td2x2x = 0x00,
            Td2x3x = 0x01,
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
            DieCutLabels = 0x0B,  // ESC i z {n2}. The status block uses StatusMediaType.
        };

        // Status block offset 11, raster spec 4 table (4).
        enum class StatusMediaType : uint8_t
        {
            NoMedia              = 0x00,
            ContinuousLengthTape = 0x4A,
            DieCutLabels         = 0x4B,
        };

        // Status block offset 22, raster spec 4 table (7).
        enum class NotificationNumber : uint8_t
        {
            NotAvailable              = 0x00,
            CoolingStarted            = 0x03,
            CoolingFinished           = 0x04,
            WaitingForPeeling         = 0x05,
            FinishedWaitingForPeeling = 0x06,
            PrinterPaused             = 0x07,
            FinishedPrinterPaused     = 0x08,
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

        using PrinterInfoFields = brother::raster::types::StatusBlockFields<
            ModelCode, ErrorInformation1Fields, ErrorInformation2Fields, StatusMediaType, NotificationNumber>;
        static_assert(sizeof(PrinterInfoFields) == 32, "PrinterInfoFields must match protocol size (32 bytes)");
        using PrinterInfo = PackedProtocolBlock<PrinterInfoFields, 32>;

        // 127-byte media blob payload of ESC i U (1Bh 69h 55h 77h 01h).
        // Field names and offsets come from the Brother PTD source bst202ed.txt.
        // The print area and offset values agree with the label table of raster spec 2.3.2 (b).
        struct __attribute__((packed)) MediaInfoFields {
            uint8_t  sensorId;              // [0]    PTD: nSensorID (63 for TD-2000 series)
            uint8_t  energyRank;            // [1]    PTD: byEnergyRank
            uint8_t  paperWidth;            // [2]    PTD: nPaperWidth/10 (mm)
            uint8_t  paperLengthMm;         // [3]    PTD: nPaperLength/10 (mm); 0 for continuous tape
            uint8_t  headDivide;            // [4]    PTD: byHeadDivide (always 0)
            uint8_t  rollWidMm;             // [5]    PTD: byRollWidMm
            uint16_t pinOffsetLeft;         // [6-7]  PTD: wPinOffsetLeft, little-endian (dots)
            uint16_t imageAreaWidthRes;     // [8-9]  PTD: nImageAreaWidthRes, little-endian (dots)
            uint16_t imageAreaLengthRes;    // [10-11] PTD: nImageAreaLengthRes, little-endian (dots); 0 for continuous
            uint8_t  dieStartPlus;          // [12]   PTD: nDieStartPlus
            uint8_t  dieStartRev;           // [13]   PTD: nDieStartRev
            uint8_t  dieStartFwd;           // [14]   PTD: nDieStartFwd
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
            // [115-126] hold the 12 values of PTD reserved_12_. Both 16-bit values below equal the
            // "Length offset" column of raster spec 2.3.2 (b): 24 dots at 203 dpi, 35 at 300 dpi.
            // They are the same for every medium of a family, and equal util::minFeedMargin.
            uint16_t lengthOffsetDots1;     // [115-116] reserved_12_[0..1]
            uint8_t  reserved_12_2to5[4];   // [117-120] reserved_12_[2..5]; 0 in every known medium
            // [121] reserved_12_[6]. Only 0 (continuous) and 1 (die-cut) occur in the known
            // media. Other values can exist, thus do not treat this field as a boolean.
            uint8_t  mediaClass;
            uint16_t lengthOffsetDots2;     // [122-123] reserved_12_[7..8]
            int8_t   detectionSensitivity;  // [124]     reserved_12_[9]
            int8_t   luminesenceSensitivity;// [125]     reserved_12_[10]
            uint8_t  reserved7;             // [126]     reserved_12_[11]
        };
        static_assert(sizeof(MediaInfoFields) == 127, "MediaInfo must match protocol size (127 bytes)");
        static_assert(std::is_trivially_copyable_v<MediaInfoFields>);

        using MediaInfo = PackedProtocolBlock<MediaInfoFields, 127>;

    }

    // PAPPL printer reasons (pappl_preason_t bits) for a status block. Error bits count
    // in every status type, not only in "error occurred".
    [[nodiscard]] unsigned statusReasons(const types::PrinterInfo& info);
    // Cooling started or printer paused (raster spec 5.4, 5.5).
    [[nodiscard]] bool pausesPrinting(const types::PrinterInfo& info);
    // Cooling finished or pause finished.
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

        // Raster spec 4: ESC i M. Bit 3 is rotate 180, bit 4 is the peeler.
        using VariousModeSettings = brother::raster::commands::ModeSettings<0x4D, types::VariousModeSettings>;
        // Raster spec 4: g (67h) 00h {n}.
        using RasterGraphicsTransfer =
            brother::raster::commands::RasterTransfer<0x67, brother::raster::commands::RasterCount::ZeroPrefixedByte>;

        constexpr std::array<uint8_t,5> additional_media_information{0x1B,0x69,0x55,0x77,0x01};

        // Raster spec 4: ESC i U (1Bh 69h 55h 77h 01h + 127 bytes).
        class AdditionalMediaInformation
        {
        public:
            explicit AdditionalMediaInformation(const types::MediaInfo& info) : mediaInformation(info) {}
            [[nodiscard]] std::vector<uint8_t> get() const
            {
                std::vector<uint8_t> data{additional_media_information.begin(), additional_media_information.end()};
                data.insert(data.end(), mediaInformation.raw.begin(), mediaInformation.raw.end());
                return data;
            }
        private:
            types::MediaInfo mediaInformation;
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
        // width and length are the media dimensions as PAPPL supplies them.
        types::MediaInfo getMediaInfoForMedia(types::ModelFamily family,
                                              util::units::MediaSize width,
                                              util::units::MediaSize length,
                                              types::MediaType type) noexcept;

        // The {n3} and {n4} bytes of ESC i z. They must agree with the media record that
        // ESC i U sent, thus they come from that record and not from the request. If they
        // disagree, the printer reports a media error. See raster spec 4, ESC i z.
        struct PrintInfoMedia
        {
            uint8_t width;      // {n3} mm
            uint8_t length;      // {n4} mm; 0 for continuous tape
            bool    lengthValid; // clear PI_LENGTH when false
        };

        [[nodiscard]] PrintInfoMedia printInfoMedia(const types::MediaInfo& info,
                                                    types::MediaType type,
                                                    util::units::MediaSize requestedLength) noexcept;

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

    // TODO: left/right were written as bare 3 with no unit stated. PAPPL takes
    // hundredths of a millimetre, so this is 0.03 mm. If 3 printer dots was meant,
    // the values are 37 (203 dpi) and 25 (300 dpi). Value kept as-is pending a
    // decision, because changing it changes the printable area.
    inline const std::map<types::ModelFamily, types::Margins> margins{
        {types::ModelFamily::Td2x2x, {
            util::units::fromPwg(3),
            util::units::fromPwg(3),
            util::units::fromPwg(0),
            util::units::fromPwg(0)
        }},
        {types::ModelFamily::Td2x3x, {
            util::units::fromPwg(3),
            util::units::fromPwg(3),
            util::units::fromPwg(0),
            util::units::fromPwg(0)
        }}
    };

    // Minimum feed margin for continuous tape per spec §2.3.3; die-cut must be 0.
    // ESC i d carries dots, unlike the PAPPL margins above which are hundredths of
    // a millimetre. Spec values, not derived: 3 mm at 203 dpi truncates to 23 dots.
    inline const std::map<types::ModelFamily, util::units::DotCount> minFeedMargin{
        {types::ModelFamily::Td2x2x, 24 * util::units::dot},  // 203 dpi
        {types::ModelFamily::Td2x3x, 35 * util::units::dot},  // 300 dpi
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

    inline constexpr const char* driverName = "brother_td_2000";
    inline constexpr const char* driverInfo = "Brother TD-2020/2120N/2130N/2030A/2125N/2125NWB/2135N/2135NWB";

    // true when this driver handles the IEEE 1284 MDL `model`.
    inline bool supportsModel(const std::string_view model) { return modelFamilyMap.contains(model); }

    // driverAttrs is not used: this driver has no vendor options.
    bool updateDriverData(pappl_pr_driver_data_t* driverData, const std::string_view& model,
                          ipp_t** driverAttrs = nullptr);
}

#endif //BR_THERMAL_TD2000_H
