#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define RR_CONTROLLER_HOST_TEST
#include "../main/camera/camera_controller.c"

/* Run the real worker synchronously; only RTOS, storage and camera transport are substituted. */
controller_ble_profile_t s_ble_profile;
static rr_settings_t settings;
static connect_state_t link;
static dji_rs_sdk_connect_result_t first_result;
static unsigned attempts, sleeps;
static bool maintenance, manual_pair;
static jmp_buf worker_done;
static TickType_t tick;
static camera_controller_state_t gopro_state;
static bool saved_pending, last_target;
static uint8_t saved_remaining;
static unsigned record_calls;
static esp_err_t save_error, record_error;
camera_protocol_t management_camera(void) { return CAMERA_PROTOCOL_DJI_RSDK; }
const rr_settings_t *management_settings(void) { return &settings; }
bool management_active(void) { return maintenance; }
esp_err_t management_stop_save(bool pending, uint8_t remaining) {
    if (save_error) return save_error;
    saved_pending = pending; saved_remaining = remaining; return ESP_OK;
}
esp_err_t management_stop_load(bool *pending, uint8_t *remaining) { *pending = saved_pending; *remaining = saved_remaining; return ESP_OK; }
bool management_stop_healthy(void) { return !save_error; }
TickType_t xTaskGetTickCount(void) { return tick; }
int64_t esp_timer_get_time(void) { return (int64_t)tick * 1000; }
void vTaskDelay(TickType_t ticks) {
    tick += ticks;
    if (ticks == 1000) {
        if (attempts == 1) {
            assert(s_phase == (first_result == DJI_RSDK_CONNECT_REJECTED ? CAMERA_PHASE_PAIR_REJECTED :
                              first_result == DJI_RSDK_CONNECT_SUBSCRIBE_FAILED ? CAMERA_PHASE_SUBSCRIBE_FAILED :
                              CAMERA_PHASE_SCAN_FAILED));
        }
        if (++sleeps >= 3) longjmp(worker_done, 1);
    }
}
void vTaskDelete(TaskHandle_t task) { (void)task; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *argument, unsigned priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)argument; (void)priority;
    if (handle) *handle = (void *)1;
    return pdPASS;
}
uint32_t esp_random(void) { return 1234; }
void ble_set_reconnect_target(const uint8_t *address, uint8_t type) { (void)address; (void)type; }
void ble_get_diagnostics(ble_diagnostics_t *d) { d->address_type = 0; }
int nvs_open(const char *name, int mode, nvs_handle_t *h) { (void)name; (void)mode; *h = 1; return ESP_OK; }
int nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length) {
    (void)h; (void)key; (void)value; (void)length; return ESP_OK;
}
int nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *length) {
    (void)h; (void)key; (void)value; (void)length; return ESP_FAIL;
}
int nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
connect_state_t connect_logic_get_state(void) { return link; }
int connect_logic_ble_init(void) { return 0; }
int connect_logic_ble_connect(bool remembered) {
    assert(remembered == !manual_pair);
    link = BLE_CONNECTED; s_ble_profile.connection_status.is_connected = true; return 0;
}
int connect_logic_ble_disconnect(void) {
    link = BLE_INIT_COMPLETE; s_ble_profile.connection_status.is_connected = false; return 0;
}
void dji_rs_sdk_init(void) {}
void dji_rs_sdk_reset(void) {}
dji_rs_sdk_connect_result_t dji_rs_sdk_connect(uint16_t code, bool remembered) {
    assert(code == 1234 && remembered == !manual_pair);
    ++attempts;
    if (attempts == 1) return first_result;
    link = PROTOCOL_CONNECTED; return DJI_RSDK_CONNECT_READY;
}
int dji_rs_sdk_set_recording(bool recording) { (void)recording; return 0; }
void dji_rs_sdk_get_snapshot(camera_snapshot_t *snapshot) { *snapshot = (camera_snapshot_t){0}; }
void dji_action2_init(dji_action2_disconnect_callback_t callback) { (void)callback; }
void dji_action2_load_profile(void) {}
bool dji_action2_get_profile(uint8_t address[6], uint8_t *type) { (void)address; (void)type; return false; }
void dji_action2_reset(void) {}
void dji_action2_detach(void) {}
esp_err_t dji_action2_begin_session(void) { return ESP_OK; }
esp_err_t dji_action2_trigger_pairing(void) { return ESP_OK; }
esp_err_t dji_action2_set_recording(bool recording) { (void)recording; return ESP_OK; }
bool dji_action2_is_ready(void) { return false; }
void dji_action2_get_state(dji_action2_state_t *state) { *state = (dji_action2_state_t){0}; }
esp_err_t gopro_init(void) { return ESP_OK; }
void gopro_start(void) {}
esp_err_t gopro_request_recording(bool recording) { ++record_calls; last_target = recording; return record_error; }
void gopro_request_pairing(void) {}
void gopro_get_state(camera_controller_state_t *state) { *state = gopro_state; }

static void run_worker(dji_rs_sdk_connect_result_t failure, bool active) {
    s_selected = CAMERA_PROTOCOL_DJI_RSDK; s_rs_saved = true;
    s_initialized = true; s_command_busy = false; s_pair_queued = false;
    s_phase = CAMERA_PHASE_IDLE; link = BLE_INIT_COMPLETE;
    s_ble_profile.connection_status.is_connected = false;
    attempts = sleeps = 0; first_result = failure; maintenance = active;
    if (!setjmp(worker_done)) auto_connect_task(NULL);
}
int main(void) {
    run_worker(DJI_RSDK_CONNECT_FAILED, false);
    assert(attempts == 2 && s_phase == CAMERA_PHASE_READY);
    run_worker(DJI_RSDK_CONNECT_SUBSCRIBE_FAILED, false);
    assert(attempts == 2 && s_phase == CAMERA_PHASE_READY);
    run_worker(DJI_RSDK_CONNECT_REJECTED, false);
    assert(attempts == 1 && s_phase == CAMERA_PHASE_PAIR_REJECTED);
    /* Explicit manual pairing clears rejection and uses first-pair verification. */
    manual_pair = true; pair_camera(false); manual_pair = false;
    assert(attempts == 2 && s_phase == CAMERA_PHASE_READY);
    run_worker(DJI_RSDK_CONNECT_FAILED, true);
    assert(attempts == 1); /* Maintenance inhibits background R SDK reconnect. */
    maintenance = false; s_selected = CAMERA_PROTOCOL_GOPRO;
    gopro_state = (camera_controller_state_t){.protocol = CAMERA_PROTOCOL_GOPRO,
        .phase = CAMERA_PHASE_READY, .recording_valid = true, .link_connected = true};
    saved_pending = true; saved_remaining = 5; save_error = ESP_FAIL;
    assert(camera_controller_request_recording(true) == ESP_FAIL && record_calls == 0);
    assert(saved_pending && saved_remaining == 5); /* START cannot resurrect an old STOP after reboot. */
    gopro_state.recording = true;
    assert(camera_controller_request_recording(false) == ESP_OK && record_calls == 1 && !last_target);
    camera_controller_state_t state; camera_controller_get_state(&state);
    assert(state.stop_persistence_error); /* Storage failure must not prevent a real STOP attempt. */
    save_error = 0; record_error = ESP_FAIL;
    assert(camera_controller_request_recording(true) == ESP_FAIL && saved_pending && saved_remaining == 5);
    record_error = 0;
    uint32_t old_sequence = s_manual_sequence;
    assert(camera_controller_request_recording(true) == ESP_OK && !saved_pending);
    assert(s_manual_sequence == old_sequence + 1);
    assert(camera_controller_persist_stop(true, 5, old_sequence) == ESP_ERR_INVALID_STATE && !saved_pending);
    camera_controller_stop_countdown(5, old_sequence); assert(s_stop_countdown == 0);
    camera_controller_stop_countdown(5, s_manual_sequence); assert(s_stop_countdown == 5);
    camera_controller_stop_countdown(0, 0); assert(s_stop_countdown == 0);
    unsigned previous_calls = record_calls;
    assert(camera_controller_automate_recording(false, old_sequence) == ESP_ERR_INVALID_STATE && record_calls == previous_calls);
    gopro_state.recording_valid = false;
    assert(camera_controller_request_recording(false) == ESP_ERR_INVALID_STATE && record_calls == previous_calls);
    puts("Camera controller transient reconnect, subscription failure, rejection and manual recovery passed.");
    return 0;
}
