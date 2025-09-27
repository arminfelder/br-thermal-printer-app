
add_library(pappl_td2000 BrotherThermal.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PAPPL REQUIRED IMPORTED_TARGET pappl)
target_link_libraries(pappl_td2000 PUBLIC PkgConfig::PAPPL)

