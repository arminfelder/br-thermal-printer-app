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

#ifndef BR_THERMAL_UNITS_H
#define BR_THERMAL_UNITS_H

#include <mp-units/compat_macros.h>
#include <mp-units/framework.h>
#include <mp-units/systems/si.h>

namespace util::units
{
    // Media dimension. PAPPL and PWG carry every media dimension in hundredths of
    // a millimetre (pappl_media_col_s::size_width, pwg_media_t::width), which is
    // exactly 10 um, so micrometres hold every PAPPL value without rounding.
    using MediaSize = mp_units::quantity<mp_units::si::micro<mp_units::si::metre>, int>;

    // One PAPPL/PWG unit: a hundredth of a millimetre.
    inline constexpr int micrometresPerPwgUnit = 10;

    // Build a MediaSize from a raw PAPPL/PWG field.
    [[nodiscard]] constexpr MediaSize fromPwg(const int value) noexcept
    {
        return value * micrometresPerPwgUnit * mp_units::si::micro<mp_units::si::metre>;
    }

    // Convert back to a raw PAPPL/PWG field (hundredths of a millimetre).
    [[nodiscard]] constexpr int toPwg(const MediaSize size) noexcept
    {
        return size.numerical_value_in(mp_units::si::micro<mp_units::si::metre>)
               / micrometresPerPwgUnit;
    }

    // A printer dot. Its own base dimension: a dot count only becomes a length once
    // a head resolution is applied, so dots never convert to millimetres by accident.
    // PAPPL margins are hundredths of a millimetre, Brother ESC i d margins are dots.
    inline constexpr struct dim_dot final : mp_units::base_dimension<"D"> {} dim_dot;
    QUANTITY_SPEC(dot_count, dim_dot);
    inline constexpr struct dot final : mp_units::named_unit<"dot", mp_units::kind_of<dot_count>> {} dot;

    using DotCount = mp_units::quantity<dot, int>;

    // Whole millimetres, truncated. The Brother protocol carries media width and
    // length as one byte of millimetres, so sub-millimetre detail is dropped.
    [[nodiscard]] constexpr int toWholeMillimetres(const MediaSize size) noexcept
    {
        return size.force_numerical_value_in(mp_units::si::milli<mp_units::si::metre>);
    }
}

#endif //BR_THERMAL_UNITS_H
