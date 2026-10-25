#pragma once
#ifdef RR_HOST_TEST
#include "management_test_platform.h"
#else
#include "esp_err.h"
#endif
#include "camera/camera_types.h"
#include "protocol.h"
#include "settings.h"

esp_err_t management_init(void);
camera_protocol_t management_camera(void);
const rr_settings_t *management_settings(void);
const rr_osd_layout_t *management_osd_layout(void);
bool management_active(void);
bool management_healthy(void);
bool management_take_control_reset(void);
/* Called exclusively by the existing UART owner (bf_bridge task). */
void management_feed(uint8_t byte);
void management_tick(void);
void management_button_pair(void);
esp_err_t management_stop_load(bool *pending, uint8_t *remaining_seconds);
esp_err_t management_stop_save(bool pending, uint8_t remaining_seconds);
bool management_stop_healthy(void);
void management_fc_state(bool valid, bool armed, bool failsafe);
bool management_ground_allowed(void);
void management_button_wifi(void);
/* Wi-Fi boot has a dedicated serialized owner; normal boots retain the UART owner. */
void management_set_reply_writer(void (*writer)(const uint8_t *, size_t));
void management_schedule_restart(void);
esp_err_t management_leave_wifi(void);
