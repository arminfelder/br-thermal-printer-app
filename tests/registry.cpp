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

#include <set>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "driver_registry.h"

namespace
{
    template <typename Map>
    void requireAllModelsResolveTo(const Map& models, const char* driverName)
    {
        for (const auto& [model, unused] : models)
        {
            INFO("model " << model);
            const auto* entry = registry::forModel(model);
            REQUIRE(entry != nullptr);
            REQUIRE(std::string_view(entry->name) == driverName);
        }
    }
}

TEST_CASE("registry - every TD-2000 model resolves to the td2000 driver", "[registry]")
{
    requireAllModelsResolveTo(drivers::td2000::modelFamilyMap, drivers::td2000::driverName);
}

TEST_CASE("registry - every PT model resolves to the pte550w driver", "[registry]")
{
    requireAllModelsResolveTo(drivers::pte550w::modelVariantMap, drivers::pte550w::driverName);
}

// The old autoadd table said "TD-2125NW" and "TD-2135NW". The Brother raster, ESC/P and
// P-touch Template references and the user guide name the models TD-2125NWB/TD-2135NWB.
TEST_CASE("registry - TD-2125NWB and TD-2135NWB are auto-added", "[registry]")
{
    REQUIRE(registry::forModel("TD-2125NWB") == registry::byName(drivers::td2000::driverName));
    REQUIRE(registry::forModel("TD-2135NWB") == registry::byName(drivers::td2000::driverName));
}

TEST_CASE("registry - unknown model and unknown driver name give nullptr", "[registry]")
{
    REQUIRE(registry::forModel("") == nullptr);
    REQUIRE(registry::forModel("QL-820NWB") == nullptr);
    REQUIRE(registry::byName("brother_unknown") == nullptr);
}

TEST_CASE("registry - driver names are unique", "[registry]")
{
    std::set<std::string> names;
    for (const auto& entry : registry::drivers)
    {
        REQUIRE(names.insert(entry.name).second);
    }
}

// Only the model check and the driver data are tested here. The callbacks need a PAPPL job.
TEST_CASE("registry - each driver fills driver data for each of its models", "[registry]")
{
    for (const auto& [model, unused] : drivers::td2000::modelFamilyMap)
    {
        pappl_pr_driver_data_t data{};
        INFO("model " << model);
        REQUIRE(registry::forModel(model)->updateDriverData(&data, model, nullptr));
        REQUIRE(data.rwriteline_cb != nullptr);
        REQUIRE(data.status_cb != nullptr);
        REQUIRE(data.num_source == 1);
        REQUIRE(std::string_view(data.media_default.size_name) == "roll_default_58x100mm");
    }
    for (const auto& [model, unused] : drivers::pte550w::modelVariantMap)
    {
        pappl_pr_driver_data_t data{};
        INFO("model " << model);
        REQUIRE(registry::forModel(model)->updateDriverData(&data, model, nullptr));
        REQUIRE(data.num_source == 2);
        REQUIRE(data.x_default == 180);
        REQUIRE(std::string_view(data.make_and_model) == std::string("Brother ") + std::string(model));
    }
    pappl_pr_driver_data_t data{};
    REQUIRE_FALSE(drivers::td2000::updateDriverData(&data, "PT-P750W"));
    REQUIRE_FALSE(drivers::pte550w::updateDriverData(&data, "TD-2020"));
}
