#include "headless_status.h"

#include <string.h>
#include "esp_log.h"
#include "betaflight/bf_control.h"
#include "management/management.h"

void headless_status_refresh(const camera_controller_state_t *state)
{
    static bool initialized;
    static camera_phase_t last_phase;
    static camera_protocol_t last_protocol;
    static uint16_t last_pairing_code;
    static char last_lines[4][17];
    static bf_osd_state_t osd_state;
    static int last_result;
    static bool last_pending;
    char lines[4][17];
    bf_camera_osd_configured(state, management_settings(), &osd_state, lines);
    if (!initialized || state->phase != last_phase || state->protocol != last_protocol ||
        state->pairing_code != last_pairing_code || state->command_pending != last_pending ||
        state->last_command_result != last_result ||
        memcmp(lines, last_lines, sizeof(lines))) {
        ESP_LOGI("CAM_STATUS", "phase=%d protocol=%d | %s | %s | %s | %s | pending=%u result=%d",
                 state->phase, state->protocol, lines[0], lines[1], lines[2], lines[3],
                 state->command_pending, state->last_command_result);
        if (state->phase == CAMERA_PHASE_VERIFYING && state->protocol != CAMERA_PROTOCOL_GOPRO) {
            ESP_LOGI("CAM_STATUS", "Pairing code: %04u; confirm on camera", state->pairing_code);
        }
        last_phase = state->phase;
        last_protocol = state->protocol;
        last_pairing_code = state->pairing_code;
        last_result = state->last_command_result;
        last_pending = state->command_pending;
        memcpy(last_lines, lines, sizeof(last_lines));
        initialized = true;
    }
}
