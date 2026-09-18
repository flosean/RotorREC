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
    *map = (bf_mode_map_t) { .mode_bit = -1, .failsafe_bit = -1 };
    if (length == 0 || length > 128 || permanent_id < 40 || permanent_id > 43) {
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
    }
    return map->mode_bit >= 0 && map->failsafe_bit >= 0;
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

void bf_camera_osd(const camera_controller_state_t *camera, char lines[2][17])
{
    bool connected = camera->phase == CAMERA_PHASE_READY;
    const char *state = !connected ? "CAM NO LINK" : !camera->recording_valid ? "CAM STATUS ?" :
        camera->saving ? "CAM BUSY" : camera->recording ? "CAM REC" : "CAM STANDBY";
    snprintf(lines[0], 17, "%s", state);
    if (connected && camera->battery_valid && camera->snapshot.battery <= 100) {
        snprintf(lines[1], 17, "CAM BAT %u%%", (unsigned)camera->snapshot.battery);
    } else {
        snprintf(lines[1], 17, "CAM BAT --");
    }
}
