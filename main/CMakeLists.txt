set(app_sources "app_main.c"
         "camera/camera_controller.c"
         "betaflight/msp_v2.c"
         "betaflight/osd_messages.c"
         "betaflight/bf_control.c"
         "betaflight/bf_bridge.c"
         "platform/boot_button.c")
set(app_requires driver esp_timer esp_app_format)

list(APPEND app_sources "camera/gopro/gopro.c" "camera/gopro/gopro_protocol.c"
     "camera/dji/dji_action2.c" "camera/dji/dji_rs_sdk.c"
     "management/protocol.c" "management/management.c")
list(APPEND app_requires bt nvs_flash dji_camera_sdk app_update mbedtls)

if(IDF_TARGET STREQUAL "esp32c6")
    list(APPEND app_sources "platform/display.c" "ui/dashboard.c")
    list(APPEND app_requires lvgl__lvgl waveshare_c6_lcd)
elseif(IDF_TARGET STREQUAL "esp32c3")
    list(APPEND app_sources "ui/headless_status.c" "ui/status_indicator.c" "platform/status_led.c")
else()
    message(FATAL_ERROR "Supported boards: ESP32-C6-LCD-1.47 and ESP32-C3-Zero")
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "camera" "camera/dji" "betaflight" "platform" "ui"
    REQUIRES ${app_requires}
)
