#include "status_indicator.h"

status_indicator_t status_indicator_select(const camera_controller_state_t *s)
{
    switch (s->phase) {
    case CAMERA_PHASE_LINK_PAUSED: return INDICATOR_PAUSED;
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
    case INDICATOR_PAUSED: return (indicator_rgb_t){0, 0, 12};
    case INDICATOR_CONNECTED: return (indicator_rgb_t){0, 12, 0};
    case INDICATOR_RECORDING: return (indicator_rgb_t){0, slow_on ? 12 : 0, 0};
    case INDICATOR_PAIRING: return (indicator_rgb_t){fast_on ? 12 : 0, 0, 0};
    case INDICATOR_UNCERTAIN: return (indicator_rgb_t){fast_on ? 12 : 0, fast_on ? 8 : 0, 0};
    default: return (indicator_rgb_t){slow_on ? 12 : 0, 0, 0};
    }
}

indicator_rgb_t status_indicator_brightness(indicator_rgb_t pixel, uint8_t percent)
{
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    /* Map the original 12/8 intensities to full-scale 255/170, keeping 5% at 12/8. */
    pixel.red = pixel.red ? (uint8_t)((255U * percent) / 100U) : 0;
    pixel.green = pixel.green ? (uint8_t)(((pixel.green == 8 ? 170U : 255U) * percent) / 100U) : 0;
    pixel.blue = pixel.blue ? (uint8_t)((255U * percent) / 100U) : 0;
    return pixel;
}
