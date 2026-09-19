cmake_minimum_required(VERSION 3.16)

# C3-Zero has no display and needs no downloaded components. Keep its build
# independent of the C6 LVGL dependency lock and managed component directory.
if(IDF_TARGET STREQUAL "esp32c3")
    set(ENV{IDF_COMPONENT_MANAGER} 0)
    set(COMPONENTS main)
endif()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rotorrec_${IDF_TARGET})

# Refuse stale single-app sdkconfigs: successful compilation must imply rollback support.
if(NOT CONFIG_PARTITION_TABLE_CUSTOM OR NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
   OR NOT CONFIG_BF_CAM_UART_ENABLED OR NOT CONFIG_BF_CAM_UART_BAUD EQUAL 115200)
    message(FATAL_ERROR "RotorREC 1.1 needs dual OTA partitions, rollback and UART at 115200. Use tools/passthrough.ps1 with a fresh build directory.")
endif()
if(IDF_TARGET STREQUAL "esp32c3")
    set(rr_partition_file "config/partitions-c3.csv")
    if(NOT CONFIG_ESPTOOLPY_FLASHSIZE_4MB OR NOT CONFIG_BF_CAM_UART_TX_GPIO EQUAL 0 OR NOT CONFIG_BF_CAM_UART_RX_GPIO EQUAL 1)
        message(FATAL_ERROR "C3 update identity requires the 4MB C3-Zero and TX=0/RX=1.")
    endif()
else()
    set(rr_partition_file "config/partitions-c6.csv")
    if(NOT CONFIG_ESPTOOLPY_FLASHSIZE_8MB OR NOT CONFIG_BF_CAM_UART_TX_GPIO EQUAL 18 OR NOT CONFIG_BF_CAM_UART_RX_GPIO EQUAL 19)
        message(FATAL_ERROR "C6 update identity requires the 8MB C6-LCD-1.47 and TX=18/RX=19.")
    endif()
endif()
if(NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL rr_partition_file)
    message(FATAL_ERROR "Use the board's ${rr_partition_file}; changing layouts requires USB installation and a new layout version.")
endif()
