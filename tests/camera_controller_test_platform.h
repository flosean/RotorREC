#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_BD_ADDR_LEN 6
#define BLE_ADDR_TYPE_RANDOM 1
#include "camera/camera_controller.h"
#include "camera/dji/dji_action2.h"
#include "camera/dji/dji_rs_sdk.h"
#include "camera/gopro/gopro.h"
#include "management/settings.h"
#include "connect_logic.h"
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef struct { uint8_t address_type; } ble_diagnostics_t;
typedef struct {
    struct { bool is_connected; } connection_status;
    uint8_t remote_bda[6];
} controller_ble_profile_t;
extern controller_ble_profile_t s_ble_profile;
camera_protocol_t management_camera(void);
const rr_settings_t *management_settings(void);
bool management_active(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *argument, unsigned priority, TaskHandle_t *handle);
uint32_t esp_random(void);
void ble_set_reconnect_target(const uint8_t *address, uint8_t type);
void ble_get_diagnostics(ble_diagnostics_t *diagnostics);
int nvs_open(const char *name, int mode, nvs_handle_t *handle);
int nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
int nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);
int nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
