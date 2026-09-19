#pragma once

#include "camera_controller.h"

esp_err_t gopro_init(void);
void gopro_start(void);
esp_err_t gopro_request_recording(bool recording);
void gopro_request_pairing(void);
void gopro_get_state(camera_controller_state_t *state);
