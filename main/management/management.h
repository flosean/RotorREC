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
bool management_active(void);
bool management_healthy(void);
bool management_take_control_reset(void);
/* Called exclusively by the existing UART owner (bf_bridge task). */
void management_feed(uint8_t byte);
void management_tick(void);
void management_button_pair(void);
