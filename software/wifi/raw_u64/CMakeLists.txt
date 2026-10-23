# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(IDF_VERSION_MAJOR LESS 6)
    message(FATAL_ERROR "raw_u64 needs ESP-IDF 6, not ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} (see software/wifi/raw_u64/README.md)")
endif()
project(bridge)
