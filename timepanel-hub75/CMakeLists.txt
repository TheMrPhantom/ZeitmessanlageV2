cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../components/dd-ota"
    "../components/timepanel-protocol")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(timepanel-hub75)
