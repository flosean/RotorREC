#pragma once
#include <stdint.h>
#ifdef RR_HOST_TEST
#include "management_test_platform.h"
#else
#include "esp_err.h"
#endif
/* 1 = all camera profiles/bonds, retain settings and enable Link Pause.
 * 2 = camera profiles/bonds, settings, layout, pending stop and Wi-Fi credentials. */
esp_err_t rr_reset_request(uint8_t scope);
esp_err_t rr_reset_apply(uint8_t default_user);
uint32_t rr_reset_pending(void);
