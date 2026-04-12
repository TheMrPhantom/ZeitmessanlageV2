#cmake_minimum_required(VERSION 3.16)
#include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#project(lora-network)

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include" "."
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash
                       PRIV_REQUIRES esp_http_client esp_https_ota app_update)