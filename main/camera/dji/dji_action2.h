#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_gap_ble_api.h"

typedef enum {
    DJI_ACTION2_IDLE,
    DJI_ACTION2_SESSION_SETUP,
    DJI_ACTION2_WAITING_APPROVAL,
    DJI_ACTION2_READY,
    DJI_ACTION2_REJECTED,
} dji_action2_stage_t;

typedef struct {
    dji_action2_stage_t stage;
    bool remembered;
    bool recording;
    bool recording_valid;
    bool saving;
    bool battery_valid;
    uint8_t battery;
    bool command_pending;
    int last_command_result;
    int8_t rssi;
    uint8_t address_type;
    uint16_t mtu;
    uint16_t gatt_attribute_count;
    uint32_t notification_count;
    bool target_service_found;
} dji_action2_state_t;

typedef void (*dji_action2_disconnect_callback_t)(void);

void dji_action2_init(dji_action2_disconnect_callback_t disconnect_callback);
void dji_action2_load_profile(void);
void dji_action2_forget_profile(void);
bool dji_action2_get_profile(uint8_t address[ESP_BD_ADDR_LEN], uint8_t *address_type);
void dji_action2_reset(void);
void dji_action2_detach(void);
esp_err_t dji_action2_begin_session(void);
esp_err_t dji_action2_trigger_pairing(void);
esp_err_t dji_action2_toggle_recording(void);
esp_err_t dji_action2_set_recording(bool recording);
bool dji_action2_is_ready(void);
void dji_action2_get_state(dji_action2_state_t *state);
