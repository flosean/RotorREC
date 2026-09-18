#include "display.h"

#include "LVGL_Driver.h"
#include "ST7789.h"
#include "lvgl.h"

void display_init(void)
{
    LCD_Init();
    LVGL_Init();
    lv_disp_set_rotation(disp, LV_DISP_ROT_270);
}

void display_process(void)
{
    lv_timer_handler();
}
