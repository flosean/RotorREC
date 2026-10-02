#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Immutable during a boot. SET_SETTINGS takes effect only after restart. */
typedef struct {
    uint8_t user_mode_id;
    uint8_t osd_lines;
    uint8_t led_percent;
    uint8_t low_battery_percent; /* 0 disables the warning. */
    bool link_paused;
    bool owns_extra_lines; /* Once opted into four lines, clear unused lines 3/4. */
} rr_settings_t;

rr_settings_t rr_settings_defaults(uint8_t user_mode_id);
bool rr_settings_valid(const rr_settings_t *settings);
uint32_t rr_settings_encode(const rr_settings_t *settings);
bool rr_settings_decode(uint32_t raw, rr_settings_t *settings);
