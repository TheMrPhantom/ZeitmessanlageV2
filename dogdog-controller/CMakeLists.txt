# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# The update check evaluates LVGL 9 dependencies even with LVGL 8 locked,
# triggering missing-Kconfig retries in ESP-IDF 5.5.1. Build the locked versions;
# dependency upgrades remain available through idf.py update-dependencies.
set(ENV{IDF_COMPONENT_CHECK_NEW_VERSION} "0")

set(EXTRA_COMPONENT_DIRS
    "../components/lora-network"
    "../components/dd-ota"
    "../components/timepanel-protocol")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dogdog-controller)
