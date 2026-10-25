#include "bf_control.h"

#include <stdio.h>
#include <string.h>

bool bf_api_supported(const uint8_t *payload, size_t length)
{
    /* Known BF API major only; do not assume future major layouts are compatible. */
    return length >= 3 && payload[0] == 0 && payload[1] == 1 && payload[2] >= 47;
}

bool bf_mode_map_parse(bf_mode_map_t *map, uint8_t permanent_id,
                       const uint8_t *payload, size_t length)
{
    *map = (bf_mode_map_t) { .mode_bit = -1, .failsafe_bit = -1, .arm_bit = -1 };
    if (length == 0 || length > 128 || (permanent_id != 0 && (permanent_id < 40 || permanent_id > 43))) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (payload[i] == permanent_id) {
            if (map->mode_bit >= 0) return false;
            map->mode_bit = (int)i;
        }
        if (payload[i] == 27) {
            if (map->failsafe_bit >= 0) return false;
            map->failsafe_bit = (int)i;
        }
        if (payload[i] == 0) {
            if (map->arm_bit >= 0) return false;
            map->arm_bit = (int)i;
        }
    }
    return map->mode_bit >= 0 && map->failsafe_bit >= 0;
}

bool bf_mode_map_read(bf_mode_map_t *map, uint8_t permanent_id,
                     bool (*read_page)(uint8_t page, uint8_t ids[32], size_t *length))
{
    uint8_t ids[128], chunk[32];
    size_t used = 0;
    *map = (bf_mode_map_t){.mode_bit = -1, .failsafe_bit = -1, .arm_bit = -1};
    for (uint8_t page = 0; page <= 4; ++page) {
        size_t length = 0;
        if (!read_page(page, chunk, &length) || length > sizeof(chunk) ||
            length > sizeof(ids) - used) return false;
        memcpy(ids + used, chunk, length);
        used += length;
        if (length < sizeof(chunk)) return bf_mode_map_parse(map, permanent_id, ids, used);
    }
    return false;
}

static bool mode_flag(const uint8_t *payload, size_t extra_bytes, int bit, bool *value)
{
    if (bit < 0 || (size_t)bit >= 32 + extra_bytes * 8) return false;
    size_t offset = bit < 32 ? 6 + (size_t)bit / 8 : 16 + (size_t)(bit - 32) / 8;
    *value = (payload[offset] & (1U << (bit % 8))) != 0;
    return true;
}

bool bf_mode_status_parse(const bf_mode_map_t *map, const uint8_t *payload,
                          size_t length, bf_mode_status_t *status)
{
    if (length < 21) return false;
    size_t extra = payload[15] & 0x0f;
    if (length < 21 + extra || payload[16 + extra] < 5) return false;
    bool failsafe;
    if (!mode_flag(payload, extra, map->mode_bit, &status->active) ||
        !mode_flag(payload, extra, map->failsafe_bit, &failsafe)) return false;
    size_t off = 17 + extra;
    uint32_t disabled = (uint32_t)payload[off] | ((uint32_t)payload[off + 1] << 8) |
        ((uint32_t)payload[off + 2] << 16) | ((uint32_t)payload[off + 3] << 24);
    /* FAILSAFE, RX_FAILSAFE, BOXFAILSAFE. Other arming blocks do not block camera use. */
    status->failsafe = failsafe || (disabled & ((1U << 1) | (1U << 2) | (1U << 4))) != 0;
    status->armed = false;
    status->arm_valid = mode_flag(payload, extra, map->arm_bit, &status->armed);
    return true;
}

bf_record_action_t bf_record_policy_step(bf_record_policy_t *p, uint32_t now_ms,
                                         bool input_valid, bool active,
                                         const camera_controller_state_t *camera)
{
    if (!input_valid) {
        *p = (bf_record_policy_t) {0};
        return BF_RECORD_NONE;
    }
    if (!p->sample_valid || active != p->candidate) {
        p->sample_valid = true;
        p->candidate = active;
        p->since_ms = now_ms;
        /* Cancel an unexecuted older switch intent immediately. */
        p->pending = false;
        return BF_RECORD_NONE;
    }
    if ((uint32_t)(now_ms - p->since_ms) < 200) return BF_RECORD_NONE;
    if (!p->low_seen) {
        if (!active) {
            p->low_seen = true;
            p->stable = false;
        }
        return BF_RECORD_NONE;
    }
    if (active != p->stable) {
        p->stable = active;
        p->pending = true;
    }
    if (!p->pending || camera->phase != CAMERA_PHASE_READY || !camera->recording_valid ||
        camera->saving || camera->command_pending) return BF_RECORD_NONE;
    /* One explicit set request per stable edge. Never retry a blind toggle. */
    p->pending = false;
    if (camera->recording == active) return BF_RECORD_NONE;
    return active ? BF_RECORD_START : BF_RECORD_STOP;
}

void bf_automation_manual(bf_automation_t *p, uint32_t now, bool target)
{
    /* The physical/manual command already went to the camera. Keep its intent,
     * but do not let an ARM start undo an intentional manual stop. */
    p->intent_valid = !target;
    p->target = target;
    p->stop_delay_active = false;
    p->attempted = true;
    p->attempted_ms = now;
    p->manual_override = true;
}

void bf_automation_restore_stop(bf_automation_t *p, uint32_t now, uint8_t remaining,
                                const rr_settings_t *s)
{
    p->intent_valid = true;
    p->target = false;
    if (remaining > s->stop_delay_seconds) remaining = s->stop_delay_seconds;
    p->stop_delay_active = remaining > 0;
    p->stop_since_ms = now - (uint32_t)(s->stop_delay_seconds - remaining) * 1000;
    p->attempted = false;
}

uint8_t bf_stop_delay_remaining(const bf_automation_t *p, uint32_t now,
                               const rr_settings_t *s)
{
    if (!p->stop_delay_active) return 0;
    uint32_t elapsed = now - p->stop_since_ms;
    uint32_t duration = (uint32_t)s->stop_delay_seconds * 1000;
    return elapsed >= duration ? 0 : (uint8_t)((duration - elapsed + 999) / 1000);
}

bf_record_action_t bf_automation_step(bf_automation_t *p, uint32_t now,
    bool valid, const bf_mode_status_t *mode, const rr_settings_t *s,
    const camera_controller_state_t *c)
{
    if (s->record_mode == RR_RECORD_OFF || s->link_paused) return BF_RECORD_NONE;
    if (s->record_mode == RR_RECORD_USER) {
        bf_record_action_t action = bf_record_policy_step(&p->user, now, valid && !mode->failsafe, mode->active, c);
        if (action != BF_RECORD_NONE) {
            p->intent_valid = action == BF_RECORD_STOP;
            p->target = action == BF_RECORD_START;
            p->stop_delay_active = false;
            p->attempted = true; p->attempted_ms = now;
            return action;
        }
    }
    bool arm_mode = s->record_mode == RR_RECORD_ARM || s->record_mode == RR_RECORD_COMBI;
    if (!valid || mode->failsafe || (arm_mode && !mode->arm_valid)) {
        p->arm_sample_valid = false;
        p->user = (bf_record_policy_t){0};
        /* Preserve a real earlier disarm intent; invalid data cannot create one. */
        return BF_RECORD_NONE;
    }
    if (arm_mode) {
        if (!p->arm_sample_valid || mode->armed != p->arm_candidate) {
            p->arm_sample_valid = true;
            p->arm_candidate = mode->armed;
            p->arm_since_ms = now;
        } else if ((uint32_t)(now - p->arm_since_ms) >= 200 &&
                   (!p->arm_initialized || p->armed != mode->armed)) {
            bool initial = !p->arm_initialized;
            bool was_armed = p->arm_initialized && p->armed;
            p->arm_initialized = true;
            p->armed = mode->armed;
            p->attempted = false;
            if (p->armed) {
                p->stop_delay_active = false;
                p->intent_valid = s->auto_record && (!initial || !p->manual_override);
                p->target = true;
                if (!initial) p->manual_override = false;
            } else if (was_armed) {
                p->intent_valid = s->auto_stop;
                p->target = false;
                p->stop_delay_active = s->auto_stop;
                p->stop_since_ms = now;
            }
        }
        /* Do not execute old stop intent during the re-arm debounce interval. */
        if (!p->arm_initialized || p->arm_candidate != p->armed ||
            (uint32_t)(now - p->arm_since_ms) < 200) return BF_RECORD_NONE;
    }
    bool ready = c->phase == CAMERA_PHASE_READY && c->recording_valid &&
        !c->saving && !c->command_pending;
    if (s->record_mode == RR_RECORD_BUTTON || s->record_mode == RR_RECORD_COMBI) {
        bf_record_policy_t *b = &p->user;
        if (!b->sample_valid || mode->active != b->candidate) {
            b->sample_valid = true; b->candidate = mode->active; b->since_ms = now;
        } else if ((uint32_t)(now - b->since_ms) >= 200) {
            if (!b->low_seen) {
                if (!mode->active) { b->low_seen = true; b->stable = false; }
            } else if (mode->active != b->stable) {
                b->stable = mode->active;
                /* Never queue a blind toggle to execute after a reconnect. */
                if (b->stable && ready) {
                    p->target = p->stop_delay_active ? true : !c->recording;
                    p->intent_valid = true;
                    p->stop_delay_active = false;
                    p->attempted = false;
                }
            }
        }
    }
    if (p->stop_delay_active) {
        if (bf_stop_delay_remaining(p, now, s)) return BF_RECORD_NONE;
        p->stop_delay_active = false;
    }
    if (!p->intent_valid || !ready) return BF_RECORD_NONE;
    if (c->recording == p->target) {
        if (!p->target) p->intent_valid = false;
        return BF_RECORD_NONE;
    }
    if (p->attempted && (uint32_t)(now - p->attempted_ms) < 20000) return BF_RECORD_NONE;
    p->attempted = true; p->attempted_ms = now;
    return p->target ? BF_RECORD_START : BF_RECORD_STOP;
}

void bf_camera_osd(const camera_controller_state_t *camera, char lines[2][17])
{
    bool connected = camera->phase == CAMERA_PHASE_READY;
    const char *state = !connected ? "CAM NO LINK" : !camera->recording_valid ? "CAM STATUS ?" :
        camera->saving ? "CAM BUSY" : camera->recording ? "CAM REC" : "CAM STANDBY";
    snprintf(lines[0], 17, "%s", state);
    if (camera->record_result != CAMERA_RECORD_OK)
        snprintf(lines[0], 17, "%s", camera_record_message(camera->record_result));
    else if (camera->stop_persistence_error)
        snprintf(lines[0], 17, "!STOP NOT SAVED");
    else if (camera->warnings)
        snprintf(lines[0], 17, "%s", camera_warning_message(camera->warnings));
    else if (camera->stop_delay_remaining)
        snprintf(lines[0], 17, "REC STOP IN %uS", camera->stop_delay_remaining);
    if (connected && camera->battery_valid && camera->snapshot.battery <= 100) {
        snprintf(lines[1], 17, "CAM BAT %u%%", (unsigned)camera->snapshot.battery);
    } else {
        snprintf(lines[1], 17, "CAM BAT --");
    }
}

static void osd_duration(uint32_t seconds, char text[10])
{
    if (seconds < 3600)
        snprintf(text, 10, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    else if (seconds < 360000)
        snprintf(text, 10, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                 (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
    else
        snprintf(text, 10, "%luH", (unsigned long)(seconds / 3600));
}

void bf_camera_osd_configured(const camera_controller_state_t *camera,
                             const rr_settings_t *settings, bf_osd_state_t *state,
                             char lines[4][17])
{
    memset(lines, 0, 4 * 17);
    if (settings->link_paused || camera->phase == CAMERA_PHASE_LINK_PAUSED) {
        state->low_battery = false;
        return;
    }
    bf_camera_osd(camera, lines);
    bool connected = camera->phase == CAMERA_PHASE_READY;
    bool battery = connected && camera->battery_valid && camera->snapshot.battery <= 100;
    unsigned threshold = settings->low_battery_percent;
    unsigned recovery = threshold > 97 ? 100 : threshold + 3;
    if (!battery || !threshold) state->low_battery = false;
    else if (camera->snapshot.battery <= threshold) state->low_battery = true;
    else if (camera->snapshot.battery >= recovery) state->low_battery = false;
    if (state->low_battery)
        snprintf(lines[1], 17, "LOW BAT %u%%", (unsigned)camera->snapshot.battery);
    if (settings->osd_lines != 4) return;

    bool telemetry = connected && camera->snapshot.valid;
    if (telemetry && camera->recording_valid && camera->recording && !camera->saving &&
        !camera->command_pending && camera->snapshot.record_time_valid &&
        camera->record_result == CAMERA_RECORD_OK && !camera->stop_persistence_error && !camera->warnings) {
        char elapsed[10];
        osd_duration(camera->snapshot.record_time, elapsed);
        if (camera->snapshot.record_time_estimated) snprintf(lines[0], 17,
            strlen(elapsed) > 7 ? "REC ~%.8s" : "CAM REC ~%.7s", elapsed);
        else snprintf(lines[0], 17, "CAM REC %.8s", elapsed);
    }
    if (!state->low_battery && battery && telemetry && camera->snapshot.remain_time_valid) {
        char remaining[10];
        osd_duration(camera->snapshot.remain_time, remaining);
        snprintf(lines[1], 17, "B%u%% R%.8s", (unsigned)camera->snapshot.battery, remaining);
    }
    snprintf(lines[2], 17, "CAM SETTINGS --");
    if (telemetry && camera->snapshot.parameters_valid && camera->snapshot.parameters[0]) {
        /* Bounded ASCII; vendor strings need not be NUL terminated. */
        unsigned i = 0;
        for (; i < 16 && camera->snapshot.parameters[i]; ++i) {
            unsigned char c = (unsigned char)camera->snapshot.parameters[i];
            lines[2][i] = c >= 32 && c <= 126 ? (char)c : '?';
        }
        lines[2][i] = '\0';
    }
    if (telemetry && camera->snapshot.remain_capacity_valid)
        snprintf(lines[3], 17, "SD %luMB", (unsigned long)camera->snapshot.remain_capacity);
    else
        snprintf(lines[3], 17, "SD --");
    if (camera->stop_delay_remaining && camera->record_result == CAMERA_RECORD_OK && !camera->stop_persistence_error && !camera->warnings)
        snprintf(lines[0], 17, "REC STOP IN %uS", camera->stop_delay_remaining);
}

void bf_camera_osd_layout(const camera_controller_state_t *camera,
    const rr_settings_t *settings, bf_osd_state_t *state, const rr_osd_layout_t *layout,
    char lines[4][17])
{
    bf_camera_osd_configured(camera, settings, state, lines);
    if (settings->link_paused || camera->phase == CAMERA_PHASE_LINK_PAUSED || !rr_osd_layout_valid(layout)) return;
    char values[7][32] = {{0}};
    static const char *tokens[] = {"{state}", "{battery}", "{elapsed}", "{remaining}", "{format}", "{space}", "{mode}"};
    snprintf(values[0], sizeof(values[0]), "%s", lines[0]);
    bool valid = camera->phase == CAMERA_PHASE_READY && camera->snapshot.valid;
    if (camera->phase == CAMERA_PHASE_READY && camera->battery_valid && camera->snapshot.battery <= 100)
        snprintf(values[1], sizeof(values[1]), "%s%u%%", state->low_battery ? "LOW BAT " : "B", camera->snapshot.battery);
    else strcpy(values[1], "B--");
    char duration[10];
    if (valid && camera->snapshot.record_time_valid) {
        osd_duration(camera->snapshot.record_time, duration);
        snprintf(values[2], sizeof(values[2]), "%s%s", camera->snapshot.record_time_estimated ? "~" : "", duration);
    } else strcpy(values[2], "--");
    if (valid && camera->snapshot.remain_time_valid) {
        osd_duration(camera->snapshot.remain_time, duration);
        snprintf(values[3], sizeof(values[3]), "R%s", duration);
    } else strcpy(values[3], "R--");
    snprintf(values[4], sizeof(values[4]), "%.20s", valid && camera->snapshot.parameters_valid ? camera->snapshot.parameters : "FORMAT --");
    if (valid && camera->snapshot.remain_capacity_valid)
        snprintf(values[5], sizeof(values[5]), "SD %luMB", (unsigned long)camera->snapshot.remain_capacity);
    else strcpy(values[5], "SD --");
    snprintf(values[6], sizeof(values[6]), "%.20s", valid && camera->snapshot.mode_valid ? camera->snapshot.mode : "MODE --");
    for (unsigned row = 0; row < settings->osd_lines; ++row) {
        if (!layout->rows[row][0]) continue;
        unsigned used = 0;
        for (unsigned i = 0; layout->rows[row][i] && used < 16;) {
            if (layout->rows[row][i] != '{') { lines[row][used++] = layout->rows[row][i++]; continue; }
            for (unsigned token = 0; token < 7; ++token) {
                size_t n = strlen(tokens[token]);
                if (strncmp(layout->rows[row] + i, tokens[token], n)) continue;
                i += (unsigned)n;
                for (unsigned j = 0; values[token][j] && used < 16; ++j) {
                    unsigned char c = (unsigned char)values[token][j];
                    lines[row][used++] = c >= 32 && c <= 126 ? (char)c : '?';
                }
                break;
            }
        }
        lines[row][used] = 0;
    }
    /* Safety messages always own row 1, even if the user moved or removed {state}. */
    if (camera->record_result != CAMERA_RECORD_OK || camera->stop_persistence_error ||
        camera->warnings || camera->stop_delay_remaining) snprintf(lines[0], 17, "%.16s", values[0]);
}
