#include "status_indicator.h"

status_indicator_t status_indicator_select(const camera_controller_state_t *s)
{
    switch (s->phase) {
    case CAMERA_PHASE_READY:
        if (!s->recording_valid || s->saving || s->command_pending) return INDICATOR_UNCERTAIN;
        return s->recording ? INDICATOR_RECORDING : INDICATOR_CONNECTED;
    case CAMERA_PHASE_SCANNING:
    case CAMERA_PHASE_VERIFYING:
    case CAMERA_PHASE_ACTION2_SESSION:
    case CAMERA_PHASE_ACTION2_PAIRING:
    case CAMERA_PHASE_RECONNECTING:
        return INDICATOR_PAIRING;
    case CAMERA_PHASE_WAITING_STATUS:
        return INDICATOR_UNCERTAIN;
    default:
        return INDICATOR_DISCONNECTED;
    }
}

indicator_rgb_t status_indicator_pixel(status_indicator_t indicator, uint32_t time_ms)
{
    bool slow_on = (time_ms % 2000) < 1000;
    bool fast_on = (time_ms % 400) < 200;
    switch (indicator) {
    case INDICATOR_CONNECTED: return (indicator_rgb_t){0, 12, 0};
    case INDICATOR_RECORDING: return (indicator_rgb_t){0, slow_on ? 12 : 0, 0};
    case INDICATOR_PAIRING: return (indicator_rgb_t){fast_on ? 12 : 0, 0, 0};
    case INDICATOR_UNCERTAIN: return (indicator_rgb_t){fast_on ? 12 : 0, fast_on ? 8 : 0, 0};
    default: return (indicator_rgb_t){slow_on ? 12 : 0, 0, 0};
    }
}
