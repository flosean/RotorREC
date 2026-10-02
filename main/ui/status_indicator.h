#pragma once
#include <stdint.h>
#include "camera/camera_types.h"

typedef enum {
    INDICATOR_DISCONNECTED,
    INDICATOR_PAIRING,
    INDICATOR_CONNECTED,
    INDICATOR_RECORDING,
    INDICATOR_UNCERTAIN,
    INDICATOR_PAUSED,
} status_indicator_t;

typedef struct { uint8_t red, green, blue; } indicator_rgb_t;
status_indicator_t status_indicator_select(const camera_controller_state_t *state);
indicator_rgb_t status_indicator_pixel(status_indicator_t indicator, uint32_t time_ms);
/* Percent of full brightness; 5 preserves the original low output. */
indicator_rgb_t status_indicator_brightness(indicator_rgb_t pixel, uint8_t percent);
