idf_component_register(
    SRCS "src/ST7789.c"
         "src/Vernon_ST7789T.c"
         "src/LVGL_Driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer lvgl__lvgl
)

