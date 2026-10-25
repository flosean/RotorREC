#pragma once

#ifndef RR_CONTROLLER_HOST_TEST
#include "esp_err.h"
#endif
#include "camera_types.h"

esp_err_t camera_controller_init(void);
void camera_controller_start(void);
void camera_controller_short_press(void);
/* Nonblocking: ESP_OK means accepted, not camera-confirmed. */
esp_err_t camera_controller_request_recording(bool recording);
esp_err_t camera_controller_automate_recording(bool recording, uint32_t manual_sequence);
esp_err_t camera_controller_persist_stop(bool pending, uint8_t remaining, uint32_t manual_sequence);
void camera_controller_stop_countdown(uint8_t seconds, uint32_t sequence);
esp_err_t camera_controller_request_pairing(void);
void camera_controller_get_state(camera_controller_state_t *state);
uint32_t camera_controller_reconnect_delay_ms(uint32_t failures);
