#include "headless_status.h"

#include <string.h>
#include "esp_log.h"
#include "betaflight/bf_control.h"

void headless_status_refresh(const camera_controller_state_t *state)
{
    static bool initialized;
    static camera_phase_t last_phase;
    static camera_protocol_t last_protocol;
    static uint16_t last_pairing_code;
    static char last_lines[2][17];
    static int last_result;
    static bool last_pending;
    char lines[2][17];
    bf_camera_osd(state, lines);
    if (!initialized || state->phase != last_phase || state->protocol != last_protocol ||
        state->pairing_code != last_pairing_code || state->command_pending != last_pending ||
        state->last_command_result != last_result ||
        strcmp(lines[0], last_lines[0]) || strcmp(lines[1], last_lines[1])) {
        ESP_LOGI("CAM_STATUS", "phase=%d protocol=%d | %s | %s | pending=%u result=%d",
                 state->phase, state->protocol, lines[0], lines[1],
                 state->command_pending, state->last_command_result);
        if (state->phase == CAMERA_PHASE_VERIFYING) {
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
