#pragma once
#ifdef RR_HOST_TEST
#include "management_test_platform.h"
#else
#include "esp_err.h"
#endif
#include <stdbool.h>

esp_err_t ground_wifi_init(void);
bool ground_wifi_active(void);
esp_err_t ground_wifi_credentials(char ssid[33], char password[17]);
esp_err_t ground_wifi_request(bool enabled);
esp_err_t ground_wifi_start(void);
void ground_wifi_exit_button(void);
