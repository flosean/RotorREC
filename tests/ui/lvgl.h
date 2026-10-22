#pragma once
/* Text-only LCD boundary: this test does not simulate pixels, fonts or SPI. */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
typedef struct { char text[256]; } lv_obj_t;
typedef int lv_font_t;
#define LV_TEXT_ALIGN_CENTER 0
static const lv_font_t lv_font_montserrat_14 = 14, lv_font_montserrat_20 = 20;
static lv_obj_t test_screen, labels[6];
static unsigned label_count;
static lv_obj_t *lv_scr_act(void) { return &test_screen; }
static void lv_obj_clean(lv_obj_t *obj) { (void)obj; label_count = 0; }
static lv_obj_t *lv_label_create(lv_obj_t *parent) {
    (void)parent; assert(label_count < 6); return &labels[label_count++];
}
static uint32_t lv_color_hex(uint32_t color) { return color; }
static void lv_obj_set_width(lv_obj_t *obj, int width) { (void)obj; (void)width; }
static void lv_obj_set_pos(lv_obj_t *obj, int x, int y) { (void)obj; (void)x; (void)y; }
static void lv_obj_set_style_text_align(lv_obj_t *obj, int align, int part) { (void)obj; (void)align; (void)part; }
static void lv_obj_set_style_text_font(lv_obj_t *obj, const lv_font_t *font, int part) { (void)obj; (void)font; (void)part; }
static void lv_obj_set_style_text_color(lv_obj_t *obj, uint32_t color, int part) { (void)obj; (void)color; (void)part; }
static void lv_obj_set_style_bg_color(lv_obj_t *obj, uint32_t color, int part) { (void)obj; (void)color; (void)part; }
static void lv_label_set_text(lv_obj_t *obj, const char *text) { snprintf(obj->text, sizeof(obj->text), "%s", text); }
static void lv_label_set_text_fmt(lv_obj_t *obj, const char *format, ...) {
    va_list args; va_start(args, format); vsnprintf(obj->text, sizeof(obj->text), format, args); va_end(args);
}
