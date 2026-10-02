#include "settings.h"

rr_settings_t rr_settings_defaults(uint8_t user_mode_id)
{
    return (rr_settings_t){.user_mode_id = user_mode_id, .osd_lines = 2,
        .led_percent = 5, .low_battery_percent = 0};
}

bool rr_settings_valid(const rr_settings_t *s)
{
    return s && s->user_mode_id >= 40 && s->user_mode_id <= 43 &&
        (s->osd_lines == 2 || s->osd_lines == 4) &&
        s->led_percent >= 1 && s->led_percent <= 100 &&
        s->low_battery_percent <= 100 && (s->osd_lines != 4 || s->owns_extra_lines);
}

uint32_t rr_settings_encode(const rr_settings_t *s)
{
    if (!rr_settings_valid(s)) return 0;
    return 0x01000000U | (uint32_t)(s->user_mode_id - 40) |
        (s->osd_lines == 4 ? 4U : 0U) | (s->link_paused ? 8U : 0U) |
        ((uint32_t)s->led_percent << 4) | ((uint32_t)s->low_battery_percent << 11) |
        (s->owns_extra_lines ? (1U << 18) : 0U);
}

bool rr_settings_decode(uint32_t raw, rr_settings_t *s)
{
    /* Version 1; reserved bits must be zero. Never reinterpret corrupt/future data. */
    if (!s || (raw & 0xfff80000U) != 0x01000000U) return false;
    rr_settings_t candidate = {.user_mode_id = (uint8_t)(40 + (raw & 3)),
        .osd_lines = (raw & 4) ? 4 : 2, .link_paused = (raw & 8) != 0,
        .led_percent = (uint8_t)((raw >> 4) & 127),
        .low_battery_percent = (uint8_t)((raw >> 11) & 127),
        .owns_extra_lines = (raw & (1U << 18)) != 0};
    if (!rr_settings_valid(&candidate)) return false;
    *s = candidate;
    return true;
}
