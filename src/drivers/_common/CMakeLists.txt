cmake_minimum_required(VERSION 3.31)

project(
        utils
        VERSION 0.1
        DESCRIPTION "shared utility functions for drivers"
        LANGUAGES CXX
)

if(NOT TARGET PAPPL::pappl)
    include(../../../cmake/PapplCups.cmake)
    pappl_cups_setup()
endif()


include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()