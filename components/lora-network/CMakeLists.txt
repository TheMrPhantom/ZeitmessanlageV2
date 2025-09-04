#cmake_minimum_required(VERSION 3.16)
#include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#project(lora-network)

idf_component_register(SRC_DIRS "src"
                        INCLUDE_DIRS "include"
                       PRIV_REQUIRES driver
                       INCLUDE_DIRS ".")