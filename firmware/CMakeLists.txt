# Pocket Squatch firmware for the Waveshare ESP32-S3-Touch-LCD-4B (ESP-IDF 5.4).
#   . ~/esp/esp-idf/export.sh && idf.py -C firmware build flash monitor
cmake_minimum_required(VERSION 3.16)

# The game and the scene are the same folders the PC build uses.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../sim" "${CMAKE_CURRENT_LIST_DIR}/../scene")

# The version updates compare: CI's run number, or 0 for a build of your own (which the
# next update then replaces, unless Automatic updates is off in menuconfig).
if(DEFINED ENV{SQUATCH_BUILD})
    set(PROJECT_VER "$ENV{SQUATCH_BUILD}")
else()
    set(PROJECT_VER "0")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pocket_squatch)
