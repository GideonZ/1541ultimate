# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(IDF_VERSION_MAJOR LESS 6)
    message(FATAL_ERROR "u64ctrl needs ESP-IDF 6, not ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} (see software/u64ctrl/README.md)")
endif()
project(u64ctrl)
# target_add_binary_data(u64ctrl.elf "/home/gideon/proj/ult64/target/u64_artix/u64_artix.runs/impl_1/u64_mk2_artix.bit" BINARY)
