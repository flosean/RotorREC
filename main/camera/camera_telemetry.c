#include "camera_telemetry.h"

void camera_telemetry_apply(camera_telemetry_t *m, uint32_t now,
                            const rr_settings_t *settings, camera_controller_state_t *s)
{
    bool ready = s->phase == CAMERA_PHASE_READY && !settings->link_paused;
    bool fresh = ready && s->recording_valid;
    if (m->protocol != s->protocol || !ready) {
        *m = (camera_telemetry_t){.protocol = s->protocol};
    }
    if (s->protocol == CAMERA_PROTOCOL_DJI_ACTION2) {
        /* Only a locally observed OFF -> ON transition establishes a start.
         * Reconnection to an already recording camera cannot reveal elapsed time. */
        if ((uint32_t)(now - m->last_fresh_ms) >= 5000) m->observed = m->timing = false;
        if (fresh) {
            if (!s->recording) m->timing = false;
            else if (m->observed && !m->was_recording) {
                m->started_ms = now; m->timing = true;
            }
            m->observed = true; m->was_recording = s->recording; m->last_fresh_ms = now;
        }
        s->snapshot.valid = fresh || (ready && s->battery_valid);
        s->snapshot.recording = s->recording;
        s->snapshot.recording_valid = fresh;
        s->snapshot.record_time_valid = fresh && m->timing;
        s->snapshot.record_time_estimated = s->snapshot.record_time_valid;
        s->snapshot.record_time = m->timing ? (uint32_t)(now - m->started_ms) / 1000 : 0;
    }
    s->warnings = 0;
    bool telemetry = ready && s->snapshot.valid;
    const camera_snapshot_t *v = &s->snapshot;
    if (telemetry) {
        if (v->storage_valid) {
            if (v->storage == 1) s->warnings |= CAMERA_WARN_SD_FULL;
            if (v->storage == 2) s->warnings |= CAMERA_WARN_SD_MISSING;
            if (v->storage == 3) s->warnings |= CAMERA_WARN_SD_ERROR;
        }
        if (v->write_speed_valid && v->write_speed_error) s->warnings |= CAMERA_WARN_SD_SLOW;
        if (v->mode_valid && !v->video_mode) s->warnings |= CAMERA_WARN_MODE;
        /* Blocking heat must remain visible even with heads-up warnings disabled. */
        if (v->temperature_valid && (v->temperature >= 2 || (settings->heads_up_warnings && v->temperature)))
            s->warnings |= CAMERA_WARN_HOT;
        if (settings->heads_up_warnings && v->cold_valid && v->cold) s->warnings |= CAMERA_WARN_COLD;
    }
    if (!ready || !s->battery_valid || v->battery > 100 || !settings->low_battery_percent)
        m->low_battery = false;
    else if (v->battery <= settings->low_battery_percent) m->low_battery = true;
    else if (v->battery >= (settings->low_battery_percent > 97 ? 100 : settings->low_battery_percent + 3))
        m->low_battery = false;
    if (!telemetry || !v->remain_time_valid || !settings->low_remaining_seconds) m->low_time = false;
    else if (v->remain_time <= settings->low_remaining_seconds) m->low_time = true;
    else if (v->remain_time >= (uint32_t)settings->low_remaining_seconds + 10) m->low_time = false;
    if (m->low_battery) s->warnings |= CAMERA_WARN_LOW_BATTERY;
    if (m->low_time) s->warnings |= CAMERA_WARN_LOW_TIME;
}
