#ifdef RR_CONTROLLER_HOST_TEST
#include "camera_controller_test_platform.h"
#else
#include "camera_controller.h"
#include "gopro/gopro.h"
#include "management/management.h"
#include <stdbool.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.h"
#include "connect_logic.h"
#include "dji/dji_action2.h"
#include "dji/dji_rs_sdk.h"
#endif
#include <stdatomic.h>
#include "recording_feedback.h"
#include "camera_telemetry.h"
static recording_feedback_t s_record_feedback;
static camera_telemetry_t s_telemetry;
static uint32_t s_feedback_generation;
static uint32_t s_manual_sequence;
static bool s_manual_target;
static uint8_t s_stop_countdown;
static atomic_flag s_request_lock = ATOMIC_FLAG_INIT;
static portMUX_TYPE s_feedback_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_initialized;
static atomic_bool s_pair_queued;
static camera_protocol_t s_selected;

static const char *TAG = "CAMERA_CONTROLLER";

static volatile camera_phase_t s_phase = CAMERA_PHASE_INITIALIZING;
static volatile camera_protocol_t s_protocol = CAMERA_PROTOCOL_NONE;
static volatile uint16_t s_pairing_code;
static volatile bool s_command_busy;
static volatile bool s_force_discovery;
static volatile int s_last_command_result;
static TaskHandle_t s_reconnect_task;
static bool s_rs_saved;
static uint8_t s_rs_profile[7];
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
        if (s_last_command_result == -2 || s_last_command_result == -3)
            connect_logic_ble_disconnect();
    } else {
        s_last_command_result = -1;
    }
    ESP_LOGI(TAG, "External record target=%u result=%d", recording, s_last_command_result);
    release_command();
    vTaskDelete(NULL);
}

static esp_err_t request_recording(bool recording)
{
    if (!s_initialized || management_active()) return ESP_ERR_INVALID_STATE;
    if (s_selected == CAMERA_PROTOCOL_GOPRO) return gopro_request_recording(recording);
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

static esp_err_t recording_request(bool recording, bool manual, uint32_t sequence)
{
    if (atomic_flag_test_and_set(&s_request_lock)) return ESP_ERR_INVALID_STATE;
    if (!manual && sequence != s_manual_sequence) {
        atomic_flag_clear(&s_request_lock);
        return ESP_ERR_INVALID_STATE;
    }
    camera_controller_state_t before;
    camera_controller_get_state(&before);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_initialized && !management_active() && before.phase == CAMERA_PHASE_READY &&
        before.recording_valid && !before.saving && !before.command_pending) {
        bool old_pending = false;
        uint8_t old_remaining = 0;
        esp_err_t loaded = management_stop_load(&old_pending, &old_remaining);
        /* Replace durable intent before START so a reset cannot resurrect an old
         * STOP against a new clip. A failed save must never block an actual STOP. */
        esp_err_t saved = management_stop_save(!recording, 0);
        result = recording && saved != ESP_OK ? saved : request_recording(recording);
        if (result != ESP_OK && loaded == ESP_OK) (void)management_stop_save(old_pending, old_remaining);
    }
    portENTER_CRITICAL(&s_feedback_mux);
    recording_feedback_request(&s_record_feedback, (uint32_t)(esp_timer_get_time() / 1000),
                               recording, result == ESP_OK);
    ++s_feedback_generation;
    if (manual && result == ESP_OK) {
        ++s_manual_sequence;
        s_manual_target = recording;
        s_stop_countdown = 0;
    }
    portEXIT_CRITICAL(&s_feedback_mux);
    atomic_flag_clear(&s_request_lock);
    return result;
}

esp_err_t camera_controller_request_recording(bool recording) { return recording_request(recording, true, 0); }
esp_err_t camera_controller_automate_recording(bool recording, uint32_t sequence)
{ return recording_request(recording, false, sequence); }
esp_err_t camera_controller_persist_stop(bool pending, uint8_t remaining, uint32_t sequence)
{
    if (atomic_flag_test_and_set(&s_request_lock)) return ESP_ERR_INVALID_STATE;
    /* An old bridge snapshot must not restore STOP after a newer manual START. */
    esp_err_t result = sequence == s_manual_sequence ? management_stop_save(pending, remaining) : ESP_ERR_INVALID_STATE;
    atomic_flag_clear(&s_request_lock);
    return result;
}
void camera_controller_stop_countdown(uint8_t seconds, uint32_t sequence)
{
    portENTER_CRITICAL(&s_feedback_mux);
    if (!seconds || sequence == s_manual_sequence) s_stop_countdown = seconds;
    portEXIT_CRITICAL(&s_feedback_mux);
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
        if (xTaskCreate(pair_camera_task, "dji_pair", 8192, NULL, 5, NULL) != pdPASS) {
            s_pair_queued = false;
            s_phase = CAMERA_PHASE_SCAN_FAILED;
        }
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

static void pair_camera(bool use_saved)
{
    if (!take_command()) {
        s_pair_queued = false;
        return;
    }
    s_pair_queued = false;
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
    bool remembered = use_saved && s_selected == CAMERA_PROTOCOL_DJI_RSDK && s_rs_saved;
    if (remembered) ble_set_reconnect_target(s_rs_profile, s_rs_profile[6]);
    if (connect_logic_ble_connect(remembered) != 0) {
        s_phase = s_ble_profile.connection_status.is_connected
            ? CAMERA_PHASE_GATT_MISMATCH
            : CAMERA_PHASE_SCAN_FAILED;
        release_command();
        return;
    }

    if (s_selected == CAMERA_PROTOCOL_DJI_RSDK) {
        s_pairing_code = (uint16_t)(esp_random() % 10000);
        s_phase = CAMERA_PHASE_VERIFYING;
        dji_rs_sdk_connect_result_t result = dji_rs_sdk_connect(s_pairing_code, remembered);
        s_protocol = CAMERA_PROTOCOL_DJI_RSDK;
        s_phase = result == DJI_RSDK_CONNECT_READY ? CAMERA_PHASE_READY :
                  result == DJI_RSDK_CONNECT_REJECTED ? CAMERA_PHASE_PAIR_REJECTED :
                  result == DJI_RSDK_CONNECT_SUBSCRIBE_FAILED ? CAMERA_PHASE_SUBSCRIBE_FAILED :
                  CAMERA_PHASE_SCAN_FAILED;
        if (result == DJI_RSDK_CONNECT_READY) {
            ble_diagnostics_t diagnostics;
            ble_get_diagnostics(&diagnostics);
            uint8_t profile[7];
            memcpy(profile, s_ble_profile.remote_bda, 6);
            profile[6] = diagnostics.address_type;
            if (!s_rs_saved || memcmp(profile, s_rs_profile, sizeof(profile))) {
                nvs_handle_t handle;
                esp_err_t saved = nvs_open("dji_rsdk", NVS_READWRITE, &handle);
                if (saved == ESP_OK) {
                    saved = nvs_set_blob(handle, "target", profile, sizeof(profile));
                    if (saved == ESP_OK) saved = nvs_commit(handle);
                    nvs_close(handle);
                }
                if (saved != ESP_OK) ESP_LOGE(TAG, "Could not save R SDK camera: %s", esp_err_to_name(saved));
                else { memcpy(s_rs_profile, profile, sizeof(profile)); s_rs_saved = true; }
            }
        }
        if (result != DJI_RSDK_CONNECT_READY) connect_logic_ble_disconnect();
        release_command();
        return;
    }

    s_protocol = CAMERA_PROTOCOL_DJI_ACTION2;
    s_phase = CAMERA_PHASE_ACTION2_SESSION;
    esp_err_t result = dji_action2_begin_session();
    if (result != ESP_OK) {
        s_phase = CAMERA_PHASE_PAIR_REJECTED;
    }
    release_command();
}

static void pair_camera_task(void *argument)
{
    pair_camera(argument != NULL);
    vTaskDelete(NULL);
}

static void auto_connect_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t address[ESP_BD_ADDR_LEN];
    uint8_t address_type;
    if (s_selected == CAMERA_PROTOCOL_DJI_ACTION2 && dji_action2_get_profile(address, &address_type)) {
        s_protocol = CAMERA_PROTOCOL_DJI_ACTION2;
        s_phase = CAMERA_PHASE_RECONNECTING;
        start_reconnect();
        vTaskDelete(NULL);
        return;
    }
    pair_camera(s_rs_saved);
    /* The BLE callback only invalidates the link. Restore the full R SDK session
     * here, including approval, version query and status subscription. */
    while (s_selected == CAMERA_PROTOCOL_DJI_RSDK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_rs_saved && !s_command_busy && !s_pair_queued && !management_active() &&
            s_phase != CAMERA_PHASE_PAIR_REJECTED &&
            connect_logic_get_state() == BLE_INIT_COMPLETE) {
            pair_camera(true);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t camera_controller_init(void)
{
    s_selected = management_camera();
    if (management_settings()->link_paused) {
        s_protocol = s_selected;
        s_phase = CAMERA_PHASE_LINK_PAUSED;
        /* BLE is deliberately never initialized; saved profiles remain intact. */
        return ESP_OK;
    }
    if (s_selected == CAMERA_PROTOCOL_DJI_ACTION2) s_pairing_code = 5160;
    if (s_selected == CAMERA_PROTOCOL_GOPRO) {
        esp_err_t result = gopro_init();
        s_initialized = result == ESP_OK;
        return result;
    }
    dji_rs_sdk_init();
    dji_action2_init(action2_disconnected);
    if (connect_logic_ble_init() != 0) {
        s_phase = CAMERA_PHASE_SCAN_FAILED;
        return ESP_FAIL;
    }
    if (s_selected == CAMERA_PROTOCOL_DJI_ACTION2) dji_action2_load_profile();
    else {
        nvs_handle_t handle;
        if (nvs_open("dji_rsdk", NVS_READONLY, &handle) == ESP_OK) {
            size_t size = sizeof(s_rs_profile);
            s_rs_saved = nvs_get_blob(handle, "target", s_rs_profile, &size) == ESP_OK &&
                size == sizeof(s_rs_profile) && s_rs_profile[6] <= BLE_ADDR_TYPE_RANDOM;
            nvs_close(handle);
        }
    }
    s_phase = CAMERA_PHASE_IDLE;
    s_initialized = true;
    return ESP_OK;
}

void camera_controller_start(void)
{
    if (!s_initialized) return;
    if (s_selected == CAMERA_PROTOCOL_GOPRO) { gopro_start(); return; }
    xTaskCreate(auto_connect_task, "dji_auto_connect", 8192, NULL, 5, NULL);
}

void camera_controller_short_press(void)
{
    if (!s_initialized || management_active()) return;
    if (s_selected == CAMERA_PROTOCOL_GOPRO) {
        camera_controller_state_t state;
        gopro_get_state(&state);
        camera_controller_request_recording(!state.recording);
        return;
    }
    dji_action2_state_t action2;
    dji_action2_get_state(&action2);
    if (s_protocol == CAMERA_PROTOCOL_DJI_ACTION2 &&
        (action2.stage == DJI_ACTION2_SESSION_SETUP || action2.stage == DJI_ACTION2_REJECTED)) {
        if (take_command()) {
            dji_action2_trigger_pairing();
            release_command();
        }
        return;
    }
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    camera_controller_request_recording(!state.recording);
}

static esp_err_t request_pairing(void)
{
    if (!s_initialized || s_command_busy || s_pair_queued) return ESP_ERR_INVALID_STATE;
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    if (state.command_pending || ((state.link_connected || state.phase == CAMERA_PHASE_READY) &&
        (!state.recording_valid || state.recording || state.saving))) return ESP_ERR_INVALID_STATE;
    esp_err_t clear = management_stop_save(false, 0);
    if (clear != ESP_OK) return clear;
    portENTER_CRITICAL(&s_feedback_mux);
    s_record_feedback = (recording_feedback_t){0};
    ++s_feedback_generation;
    portEXIT_CRITICAL(&s_feedback_mux);
    if (s_selected == CAMERA_PROTOCOL_GOPRO) { gopro_request_pairing(); return ESP_OK; }
    if (s_command_busy || atomic_exchange(&s_pair_queued, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_reconnect_task != NULL) {
        s_force_discovery = true;
        return ESP_OK;
    }
    s_phase = CAMERA_PHASE_SCANNING;
    if (xTaskCreate(pair_camera_task, "dji_pair", 8192, NULL, 5, NULL) == pdPASS) return ESP_OK;
    s_phase = CAMERA_PHASE_SCAN_FAILED;
    s_pair_queued = false;
    return ESP_ERR_NO_MEM;
}

esp_err_t camera_controller_request_pairing(void)
{
    if (atomic_flag_test_and_set(&s_request_lock)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = request_pairing();
    atomic_flag_clear(&s_request_lock);
    return result;
}

static void get_raw_state(camera_controller_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (management_settings()->link_paused) {
        *state = (camera_controller_state_t){.protocol = s_selected, .phase = CAMERA_PHASE_LINK_PAUSED};
        return;
    }
    if (!s_initialized) { *state = (camera_controller_state_t){0}; return; }
    if (s_selected == CAMERA_PROTOCOL_GOPRO) { gopro_get_state(state); return; }
    *state = (camera_controller_state_t) {
        .protocol = s_protocol,
        .phase = s_phase,
        .pairing_code = s_pairing_code,
        .last_command_result = s_last_command_result,
        .command_pending = s_command_busy || s_pair_queued,
        .link_connected = s_ble_profile.connection_status.is_connected,
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
        state->command_pending = action2.command_pending || s_command_busy || s_pair_queued;
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
        state->recording_valid = state->snapshot.recording_valid && link == PROTOCOL_CONNECTED;
        state->battery_valid = state->snapshot.valid && state->snapshot.battery <= 100;
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

void camera_controller_get_state(camera_controller_state_t *state)
{
    if (!state) return;
    portENTER_CRITICAL(&s_feedback_mux);
    uint32_t generation = s_feedback_generation;
    portEXIT_CRITICAL(&s_feedback_mux);
    get_raw_state(state);
    portENTER_CRITICAL(&s_feedback_mux);
    camera_telemetry_apply(&s_telemetry, (uint32_t)(esp_timer_get_time() / 1000), management_settings(), state);
    /* A snapshot taken before a new request cannot confirm that request. */
    if (generation == s_feedback_generation)
        recording_feedback_apply(&s_record_feedback, (uint32_t)(esp_timer_get_time() / 1000), state);
    else
        state->record_result = s_record_feedback.result;
    state->manual_command_sequence = s_manual_sequence;
    state->manual_record_target = s_manual_target;
    state->stop_delay_remaining = s_stop_countdown;
    state->stop_persistence_error = !management_stop_healthy();
    portEXIT_CRITICAL(&s_feedback_mux);
}
