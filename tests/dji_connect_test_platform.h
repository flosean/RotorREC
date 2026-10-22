#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define __attribute__(x)
#pragma warning(push)
#pragma warning(disable:4200)
#include "dji_protocol_data_structures.h"
#pragma warning(pop)
#undef __attribute__
#include "enums_logic.h"
#include "connect_logic.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
typedef struct { void *structure; size_t length; } CommandResult;
typedef struct {
    struct { bool is_connected; } connection_status;
    struct { bool notify_char_handle_found, write_char_handle_found; } handle_discovery;
    uint16_t conn_id, notify_char_handle;
} test_ble_profile_t;
extern test_ble_profile_t s_ble_profile;
extern bool camera_status_initialized;
void receive_camera_notify_handler(const uint8_t *data, size_t length);
void ble_set_notify_callback(void (*callback)(const uint8_t *, size_t));
void ble_set_state_callback(void (*callback)(void));
void ble_set_reconnecting(bool reconnecting);
int ble_init(void);
int ble_start_scanning_and_connect(void);
bool ble_connect_attempt_finished(void);
int ble_cancel_pending_connect(void);
int ble_disconnect(void);
int ble_register_notify(uint16_t connection, uint16_t handle);
int ble_start_advertising(void);
void vTaskDelay(unsigned ticks);
uint16_t generate_seq(void);
CommandResult send_command(uint8_t set, uint8_t id, uint8_t type,
    const void *frame, uint16_t seq, int timeout);
int data_wait_for_result_by_cmd(uint8_t set, uint8_t id, int timeout,
    uint16_t *seq, void **result, size_t *length);
