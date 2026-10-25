#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { RR_RECORD_USER, RR_RECORD_ARM, RR_RECORD_BUTTON,
               RR_RECORD_COMBI, RR_RECORD_OFF } rr_record_mode_t;

/* Immutable during a boot. SET_SETTINGS takes effect only after restart. */
typedef struct {
    uint8_t user_mode_id;
    uint8_t osd_lines;
    uint8_t led_percent;
    uint8_t low_battery_percent; /* 0 disables the warning. */
    bool link_paused;
    bool owns_extra_lines; /* Once opted into four lines, clear unused lines 3/4. */
    rr_record_mode_t record_mode;
    uint8_t stop_delay_seconds;
    bool auto_record;
    bool auto_stop;
    uint16_t low_remaining_seconds; /* 0 disables; only confirmed telemetry is used. */
    bool heads_up_warnings;
} rr_settings_t;

rr_settings_t rr_settings_defaults(uint8_t user_mode_id);
bool rr_settings_valid(const rr_settings_t *settings);
uint32_t rr_settings_encode(const rr_settings_t *settings);
bool rr_settings_decode(uint32_t raw, rr_settings_t *settings);
uint32_t rr_control_encode(const rr_settings_t *settings);
bool rr_control_decode(uint32_t raw, rr_settings_t *settings);

/* Empty rows retain the built-in layout. Templates expand to at most 16 OSD cells. */
typedef struct { uint8_t version; char rows[4][64]; } rr_osd_layout_t;
bool rr_osd_layout_valid(const rr_osd_layout_t *layout);
