#include "camera_controller.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.h"
#include "connect_logic.h"
#include "dji/dji_action2.h"
#include "dji/dji_rs_sdk.h"

static const char *TAG = "CAMERA_CONTROLLER";

static volatile camera_phase_t s_phase = CAMERA_PHASE_INITIALIZING;
static volatile camera_protocol_t s_protocol = CAMERA_PROTOCOL_NONE;
static volatile uint16_t s_pairing_code;
static volatile bool s_command_busy;
static volatile bool s_force_discovery;
static volatile int s_last_command_result;
static TaskHandle_t s_reconnect_task;
static portMUX_TYPE s_command_mux = portMUX_INITIALIZER_UNLOCKED;

static bool take_command(void)
{
    portENTER_CRITICAL(&s_command_mux);
    bool available = !s_command_busy;
    if (available) s_command_busy = true;
    portEXIT_CRITICAL(&s_command_mux);
    return available;
}

static void release_command(void)
{
    portENTER_CRITICAL(&s_command_mux);
    s_command_busy = false;
    portEXIT_CRITICAL(&s_command_mux);
}

static void set_recording_task(void *argument)
{
    bool recording = (uintptr_t)argument != 0;
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    if (state.phase != CAMERA_PHASE_READY || !state.recording_valid || state.saving) {
        s_last_command_result = -1;
    } else if (state.protocol == CAMERA_PROTOCOL_DJI_ACTION2) {
        s_last_command_result = dji_action2_set_recording(recording);
    } else if (state.protocol == CAMERA_PROTOCOL_DJI_RSDK) {
        s_last_command_result = dji_rs_sdk_set_recording(recording);
    } else {
        s_last_command_result = -1;
    }
    ESP_LOGI(TAG, "External record target=%u result=%d", recording, s_last_command_result);
    release_command();
    vTaskDelete(NULL);
}

esp_err_t camera_controller_request_recording(bool recording)
{
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    if (state.phase != CAMERA_PHASE_READY || !state.recording_valid || state.saving ||
        state.command_pending || !take_command()) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(set_recording_task, "camera_record", 4096,
                    (void *)(uintptr_t)recording, 5, NULL) != pdPASS) {
        release_command();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void pair_camera_task(void *argument);

uint32_t camera_controller_reconnect_delay_ms(uint32_t failures)
{
    (void)failures;
    return 1000U;
}

static void start_reconnect(void);

static void action2_disconnected(void)
{
    s_protocol = CAMERA_PROTOCOL_DJI_ACTION2;
    s_phase = CAMERA_PHASE_RECONNECTING;
    ESP_LOGW(TAG, "[DEBUG-A2-RECONNECT] link lost; starting persistent reconnect");
    start_reconnect();
}

static void reconnect_task(void *argument)
{
    (void)argument;
    uint32_t failures = 0;
    uint8_t address[ESP_BD_ADDR_LEN];
    uint8_t address_type;

    TickType_t last_attempt = xTaskGetTickCount() - pdMS_TO_TICKS(1000);

    while (!dji_action2_is_ready() && !s_force_discovery &&
           dji_action2_get_profile(address, &address_type)) {
        uint32_t delay_ms = camera_controller_reconnect_delay_ms(failures);
        s_phase = CAMERA_PHASE_RECONNECTING;
        ESP_LOGI(TAG, "[DEBUG-A2-RECONNECT] attempt=%lu delay=%lums",
                 (unsigned long)(failures + 1), (unsigned long)delay_ms);
        TickType_t elapsed = xTaskGetTickCount() - last_attempt;
        if (elapsed < pdMS_TO_TICKS(delay_ms)) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms) - elapsed);
        }
        last_attempt = xTaskGetTickCount();

        ble_set_reconnect_target(address, address_type);
        if (connect_logic_ble_connect(true) == 0) {
            ESP_LOGI(TAG, "[DEBUG-A2-RECONNECT] BLE restored; restoring session");
            dji_action2_begin_session();
            for (int i = 0; i < 100 && !dji_action2_is_ready() &&
                            s_ble_profile.connection_status.is_connected; ++i) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (dji_action2_is_ready()) {
                s_phase = CAMERA_PHASE_READY;
                ESP_LOGI(TAG, "[DEBUG-A2-RECONNECT] Action 2 session restored");
                break;
            }
            dji_action2_detach();
            if (s_ble_profile.connection_status.is_connected) {
                connect_logic_ble_disconnect();
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        ++failures;
    }

    bool start_discovery = s_force_discovery;
    if (start_discovery) {
        dji_action2_detach();
        if (s_ble_profile.connection_status.is_connected) {
            connect_logic_ble_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        s_force_discovery = false;
    }
    s_reconnect_task = NULL;
    if (start_discovery) {
        xTaskCreate(pair_camera_task, "dji_pair", 8192, NULL, 5, NULL);
    }
    vTaskDelete(NULL);
}

static void start_reconnect(void)
{
    if (s_reconnect_task != NULL) {
        return;
    }
    BaseType_t created = xTaskCreate(reconnect_task, "action2_reconnect", 8192,
                                     NULL, 5, &s_reconnect_task);
    if (created != pdPASS) {
        s_reconnect_task = NULL;
        s_phase = CAMERA_PHASE_SCAN_FAILED;
        ESP_LOGE(TAG, "Could not start Action 2 reconnect task");
    }
}

static void pair_camera_task(void *argument)
{
    (void)argument;
    if (!take_command()) {
        vTaskDelete(NULL);
        return;
    }
    s_last_command_result = 0;
    s_protocol = CAMERA_PROTOCOL_NONE;

    connect_state_t state = connect_logic_get_state();
    if (state == BLE_CONNECTED || state == PROTOCOL_CONNECTED) {
        dji_action2_detach();
        connect_logic_ble_disconnect();
        vTaskDelay(pdMS_TO_TICKS(800));
    }

    dji_rs_sdk_reset();
    dji_action2_reset();
    s_phase = CAMERA_PHASE_SCANNING;
    if (connect_logic_ble_connect(false) != 0) {
        s_phase = s_ble_profile.connection_status.is_connected
            ? CAMERA_PHASE_GATT_MISMATCH
            : CAMERA_PHASE_SCAN_FAILED;
        release_command();
        vTaskDelete(NULL);
        return;
    }

    s_pairing_code = (uint16_t)(esp_random() % 10000);
    s_phase = CAMERA_PHASE_VERIFYING;
    dji_rs_sdk_connect_result_t rsdk_result = dji_rs_sdk_connect(s_pairing_code);
    if (rsdk_result != DJI_RSDK_CONNECT_FAILED) {
        s_protocol = CAMERA_PROTOCOL_DJI_RSDK;
        dji_action2_forget_profile();
        s_phase = rsdk_result == DJI_RSDK_CONNECT_READY
            ? CAMERA_PHASE_READY
            : CAMERA_PHASE_SUBSCRIBE_FAILED;
        release_command();
        vTaskDelete(NULL);
        return;
    }

    /* Action 2 ignores the public R SDK handshake and uses legacy DUML. */
    vTaskDelay(pdMS_TO_TICKS(800));
    s_phase = CAMERA_PHASE_SCANNING;
    if (connect_logic_ble_connect(false) != 0) {
        s_phase = CAMERA_PHASE_SCAN_FAILED;
        release_command();
        vTaskDelete(NULL);
        return;
    }

    s_protocol = CAMERA_PROTOCOL_DJI_ACTION2;
    s_phase = CAMERA_PHASE_ACTION2_SESSION;
    esp_err_t result = dji_action2_begin_session();
    if (result != ESP_OK) {
        s_phase = CAMERA_PHASE_PAIR_REJECTED;
    }
    release_command();
    vTaskDelete(NULL);
}

static void auto_connect_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t address[ESP_BD_ADDR_LEN];
    uint8_t address_type;
    if (dji_action2_get_profile(address, &address_type)) {
        s_protocol = CAMERA_PROTOCOL_DJI_ACTION2;
        s_phase = CAMERA_PHASE_RECONNECTING;
        start_reconnect();
        vTaskDelete(NULL);
        return;
    }
    pair_camera_task(NULL);
}

esp_err_t camera_controller_init(void)
{
    dji_rs_sdk_init();
    dji_action2_init(action2_disconnected);
    if (connect_logic_ble_init() != 0) {
        s_phase = CAMERA_PHASE_SCAN_FAILED;
        return ESP_FAIL;
    }
    dji_action2_load_profile();
    s_phase = CAMERA_PHASE_IDLE;
    return ESP_OK;
}

void camera_controller_start(void)
{
    xTaskCreate(auto_connect_task, "dji_auto_connect", 8192, NULL, 5, NULL);
}

void camera_controller_short_press(void)
{
    if (!take_command()) {
        return;
    }

    dji_action2_state_t action2;
    dji_action2_get_state(&action2);
    if (s_protocol == CAMERA_PROTOCOL_DJI_ACTION2) {
        if (action2.stage == DJI_ACTION2_READY) {
            dji_action2_toggle_recording();
        } else if (action2.stage == DJI_ACTION2_SESSION_SETUP ||
                   action2.stage == DJI_ACTION2_REJECTED) {
            dji_action2_trigger_pairing();
        }
        release_command();
        return;
    }

    s_last_command_result = dji_rs_sdk_toggle_recording();
    release_command();
}

void camera_controller_request_pairing(void)
{
    if (s_command_busy) {
        return;
    }
    if (s_reconnect_task != NULL) {
        s_force_discovery = true;
        return;
    }
    xTaskCreate(pair_camera_task, "dji_pair", 8192, NULL, 5, NULL);
}

void camera_controller_get_state(camera_controller_state_t *state)
{
    if (state == NULL) {
        return;
    }
    *state = (camera_controller_state_t) {
        .protocol = s_protocol,
        .phase = s_phase,
        .pairing_code = s_pairing_code,
        .last_command_result = s_last_command_result,
        .command_pending = s_command_busy,
    };

    dji_rs_sdk_get_snapshot(&state->snapshot);
    dji_action2_state_t action2;
    dji_action2_get_state(&action2);
    if (s_protocol == CAMERA_PROTOCOL_DJI_ACTION2) {
        state->last_command_result = action2.last_command_result;
        state->recording = action2.recording;
        state->recording_valid = action2.recording_valid;
        state->saving = action2.saving;
        state->battery_valid = action2.battery_valid;
        state->snapshot = (camera_snapshot_t) { .battery = action2.battery };
        state->command_pending = action2.command_pending || s_command_busy;
        state->rssi = action2.rssi;
        state->address_type = action2.address_type;
        state->mtu = action2.mtu;
        state->gatt_attribute_count = action2.gatt_attribute_count;
        state->notification_count = action2.notification_count;
        state->target_service_found = action2.target_service_found;
        if (s_phase != CAMERA_PHASE_RECONNECTING) {
            switch (action2.stage) {
                case DJI_ACTION2_SESSION_SETUP:
                    state->phase = CAMERA_PHASE_ACTION2_SESSION;
                    break;
                case DJI_ACTION2_WAITING_APPROVAL:
                    state->phase = CAMERA_PHASE_ACTION2_PAIRING;
                    break;
                case DJI_ACTION2_READY:
                    state->phase = CAMERA_PHASE_READY;
                    break;
                case DJI_ACTION2_REJECTED:
                    state->phase = CAMERA_PHASE_PAIR_REJECTED;
                    break;
                default:
                    break;
            }
        }
    }

    connect_state_t link = connect_logic_get_state();
    if (s_protocol == CAMERA_PROTOCOL_DJI_RSDK) {
        state->recording = state->snapshot.recording;
        state->recording_valid = state->snapshot.valid && link == PROTOCOL_CONNECTED;
        state->battery_valid = state->recording_valid && state->snapshot.battery <= 100;
        if (link != PROTOCOL_CONNECTED && state->phase == CAMERA_PHASE_READY) {
            state->phase = CAMERA_PHASE_WAITING_STATUS;
        }
    }
    if (state->phase == CAMERA_PHASE_IDLE) {
        if (link == BLE_NOT_INIT) {
            state->phase = CAMERA_PHASE_INITIALIZING;
        } else if (link == BLE_SEARCHING) {
            state->phase = CAMERA_PHASE_SCANNING;
        }
    }
}
