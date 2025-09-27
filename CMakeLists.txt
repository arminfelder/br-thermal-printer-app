cmake_minimum_required(VERSION 4.0)
project(pappl_td2000)

set(CMAKE_CXX_STANDARD 26)

include_directories(include)
include_directories(external/td2000)

add_subdirectory(external/td2000)

add_subdirectory(src)
add_subdirectory(apps)

