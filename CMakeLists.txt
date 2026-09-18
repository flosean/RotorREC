cmake_minimum_required(VERSION 3.16)

# C3-Zero has no display and needs no downloaded components. Keep its build
# independent of the C6 LVGL dependency lock and managed component directory.
if(IDF_TARGET STREQUAL "esp32c3")
    set(ENV{IDF_COMPONENT_MANAGER} 0)
    set(COMPONENTS main)
endif()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rotorrec)
