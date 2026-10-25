#pragma once
#include "camera_types.h"
#include "management/settings.h"

typedef struct {
    camera_protocol_t protocol;
    bool observed, was_recording, timing;
    bool low_battery, low_time;
    uint32_t last_fresh_ms, started_ms;
} camera_telemetry_t;

void camera_telemetry_apply(camera_telemetry_t *monitor, uint32_t now_ms,
                            const rr_settings_t *settings, camera_controller_state_t *state);
