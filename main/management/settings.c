#include "settings.h"
#include <string.h>

rr_settings_t rr_settings_defaults(uint8_t user_mode_id)
{
    return (rr_settings_t){.user_mode_id = user_mode_id, .osd_lines = 2,
        .led_percent = 5, .low_battery_percent = 0, .record_mode = RR_RECORD_USER,
        .stop_delay_seconds = 5, .auto_record = true, .auto_stop = true,
        .heads_up_warnings = true};
}

bool rr_settings_valid(const rr_settings_t *s)
{
    return s && s->user_mode_id >= 40 && s->user_mode_id <= 43 &&
        (s->osd_lines == 2 || s->osd_lines == 4) &&
        s->led_percent >= 1 && s->led_percent <= 100 &&
        s->low_battery_percent <= 100 && (s->osd_lines != 4 || s->owns_extra_lines) &&
        s->record_mode >= RR_RECORD_USER && s->record_mode <= RR_RECORD_OFF &&
        s->stop_delay_seconds <= 30 && s->low_remaining_seconds <= 3600;
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
        .owns_extra_lines = (raw & (1U << 18)) != 0,
        .record_mode = RR_RECORD_USER, .stop_delay_seconds = 5,
        .auto_record = true, .auto_stop = true, .heads_up_warnings = true};
    if (!rr_settings_valid(&candidate)) return false;
    *s = candidate;
    return true;
}

uint32_t rr_control_encode(const rr_settings_t *s)
{
    if (!rr_settings_valid(s)) return 0;
    return 0x01000000U | (uint32_t)s->record_mode | ((uint32_t)s->stop_delay_seconds << 3) |
        (s->auto_record ? 1U << 8 : 0) | (s->auto_stop ? 1U << 9 : 0) |
        ((uint32_t)s->low_remaining_seconds << 10) | (s->heads_up_warnings ? 1U << 22 : 0);
}

bool rr_control_decode(uint32_t raw, rr_settings_t *s)
{
    if (!s || (raw & 0xff800000U) != 0x01000000U) return false;
    rr_settings_t next = *s;
    next.record_mode = (rr_record_mode_t)(raw & 7);
    next.stop_delay_seconds = (uint8_t)((raw >> 3) & 31);
    next.auto_record = (raw & (1U << 8)) != 0;
    next.auto_stop = (raw & (1U << 9)) != 0;
    next.low_remaining_seconds = (uint16_t)((raw >> 10) & 4095);
    next.heads_up_warnings = (raw & (1U << 22)) != 0;
    if (!rr_settings_valid(&next)) return false;
    *s = next;
    return true;
}

bool rr_osd_layout_valid(const rr_osd_layout_t *layout)
{
    if (!layout || layout->version != 1) return false;
    static const char *tokens[] = {"{state}", "{battery}", "{elapsed}", "{remaining}", "{format}", "{space}", "{mode}"};
    for (unsigned row = 0; row < 4; ++row) {
        const char *text = layout->rows[row];
        if (!memchr(text, 0, 64)) return false;
        unsigned count = 0;
        for (unsigned i = 0; text[i];) {
            if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126 || text[i] == '}') return false;
            if (text[i] != '{') { ++i; continue; }
            bool found = false;
            for (unsigned t = 0; t < sizeof(tokens) / sizeof(tokens[0]); ++t) {
                size_t n = strlen(tokens[t]);
                if (!strncmp(text + i, tokens[t], n)) { i += (unsigned)n; found = true; break; }
            }
            if (!found || ++count > 8) return false;
        }
    }
    return true;
}
