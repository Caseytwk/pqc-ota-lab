cmake_minimum_required(VERSION 3.16)

# Firmware version shown on boot and compared during OTA.
# Build v1 and v2 with: idf.py -B build_v2 -DFW_VERSION=2.0.0 build
if(NOT DEFINED FW_VERSION)
    set(FW_VERSION "1.0.0")
endif()
set(PROJECT_VER "${FW_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pqc_ota_lab)
