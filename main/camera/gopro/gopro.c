#include "gopro.h"
#include "gopro_protocol.h"

#include <stdatomic.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "GOPRO";
/* A single worker owns the connection, packet assemblers and all GATT writes. */
typedef enum {
    EV_REGISTER, EV_SCAN_PARAMS, EV_SCAN_START, EV_SCAN_STOP, EV_SCAN_RESULT,
    EV_OPEN, EV_AUTH, EV_SERVICE, EV_SEARCH_DONE, EV_NOTIFY_REGISTER,
    EV_DESCRIPTOR, EV_WRITE, EV_NOTIFY, EV_DISCONNECT,
} event_kind_t;
typedef struct {
    event_kind_t kind;
    int status;
    esp_gatt_if_t interface;
    uint16_t conn, handle, end, length;
    esp_bd_addr_t address;
    uint8_t address_type;
    int8_t rssi;
    uint8_t data[244];
} event_t;

static QueueHandle_t s_events, s_commands;
static TaskHandle_t s_worker;
static atomic_bool s_overflow, s_pair_requested;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static camera_controller_state_t s_state;
static TickType_t s_status_tick;
static esp_gatt_if_t s_interface = ESP_GATT_IF_NONE;
static uint16_t s_conn;
static bool s_connected, s_remembered;
static esp_bd_addr_t s_address;
static uint8_t s_address_type;
static uint16_t s_service_start[2], s_service_end[2];
/* Command, settings, query, network-management request/response pairs. */
static uint16_t s_write[4], s_notify[4];
static gopro_rx_t s_rx[4];
static esp_ble_scan_params_t s_scan = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50, .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
};

static void post(const event_t *event)
{
    if (xQueueSend(s_events, event, 0) != pdTRUE) atomic_store(&s_overflow, true);
}

static esp_bt_uuid_t gp_uuid(uint16_t id)
{
    esp_bt_uuid_t uuid = {.len = ESP_UUID_LEN_128, .uuid.uuid128 = {
        0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0x46, 0x90,
        0xe3, 0x11, 0x8d, 0xaa, 0, 0, 0xf9, 0xb5,
    }};
    uuid.uuid.uuid128[12] = id & 0xff;
    uuid.uuid.uuid128[13] = id >> 8;
    return uuid;
}

static void gap_callback(esp_gap_ble_cb_event_t kind, esp_ble_gap_cb_param_t *p)
{
    event_t event = {0};
    switch (kind) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        event.kind = EV_SCAN_PARAMS; event.status = p->scan_param_cmpl.status; break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        event.kind = EV_SCAN_START; event.status = p->scan_start_cmpl.status; break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        event.kind = EV_SCAN_STOP; event.status = p->scan_stop_cmpl.status; break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT ||
            !gopro_advertisement(p->scan_rst.ble_adv, p->scan_rst.adv_data_len)) return;
        event.kind = EV_SCAN_RESULT;
        memcpy(event.address, p->scan_rst.bda, sizeof(event.address));
        event.address_type = p->scan_rst.ble_addr_type;
        event.rssi = p->scan_rst.rssi;
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true);
        return;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        event.kind = EV_AUTH;
        event.status = p->ble_security.auth_cmpl.success ? 0 : ESP_FAIL;
        memcpy(event.address, p->ble_security.auth_cmpl.bd_addr, sizeof(event.address));
        break;
    default: return;
    }
    post(&event);
}

static void gatt_callback(esp_gattc_cb_event_t kind, esp_gatt_if_t interface,
                          esp_ble_gattc_cb_param_t *p)
{
    event_t event = {.interface = interface};
    switch (kind) {
    case ESP_GATTC_REG_EVT:
        event.kind = EV_REGISTER; event.status = p->reg.status; break;
    case ESP_GATTC_OPEN_EVT:
        event.kind = EV_OPEN; event.status = p->open.status; event.conn = p->open.conn_id;
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        esp_bt_uuid_t management = gp_uuid(0x0090);
        const esp_bt_uuid_t *uuid = &p->search_res.srvc_id.uuid;
        if (uuid->len == ESP_UUID_LEN_16 && uuid->uuid.uuid16 == 0xfea6) event.status = 0;
        else if (uuid->len == ESP_UUID_LEN_128 &&
                 memcmp(uuid->uuid.uuid128, management.uuid.uuid128, 16) == 0) event.status = 1;
        else return;
        event.kind = EV_SERVICE; event.conn = p->search_res.conn_id;
        event.handle = p->search_res.start_handle; event.end = p->search_res.end_handle;
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        event.kind = EV_SEARCH_DONE; event.status = p->search_cmpl.status;
        event.conn = p->search_cmpl.conn_id; break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        event.kind = EV_NOTIFY_REGISTER; event.status = p->reg_for_notify.status;
        event.handle = p->reg_for_notify.handle; break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        event.kind = EV_DESCRIPTOR; event.status = p->write.status;
        event.conn = p->write.conn_id; event.handle = p->write.handle; break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        event.kind = EV_WRITE; event.status = p->write.status;
        event.conn = p->write.conn_id; event.handle = p->write.handle; break;
    case ESP_GATTC_NOTIFY_EVT:
        if (p->notify.value_len > sizeof(event.data)) {
            atomic_store(&s_overflow, true); return;
        }
        event.kind = EV_NOTIFY; event.conn = p->notify.conn_id;
        event.handle = p->notify.handle; event.length = p->notify.value_len;
        memcpy(event.data, p->notify.value, event.length); break;
    case ESP_GATTC_DISCONNECT_EVT:
        event.kind = EV_DISCONNECT; event.conn = p->disconnect.conn_id; break;
    default: return;
    }
    post(&event);
}

static void phase(camera_phase_t value)
{
    portENTER_CRITICAL(&s_mux);
    s_state.phase = value;
    if (value != CAMERA_PHASE_READY) {
        s_state.recording_valid = false;
        s_state.battery_valid = false;
        s_state.snapshot.valid = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

/* Process connection-wide events even while awaiting a command response. */
static bool next_event(event_t *event, TickType_t deadline)
{
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        if (atomic_load(&s_overflow)) return false;
        if (xQueueReceive(s_events, event, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        if (event->kind == EV_DISCONNECT) {
            if (s_connected && event->conn == s_conn) {
                s_connected = false;
                phase(CAMERA_PHASE_RECONNECTING);
                return false;
            }
            continue;
        }
        if (event->kind == EV_SERVICE) {
            s_service_start[event->status] = event->handle;
            s_service_end[event->status] = event->end;
            continue;
        }
        return true;
    }
    return false;
}

static bool wait_event(event_kind_t kind, uint16_t handle, uint32_t timeout, event_t *out)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout);
    while (next_event(out, deadline)) {
        if (out->kind == kind && (!handle || out->handle == handle)) return out->status == 0;
    }
    return false;
}

/* No shutter retries: a missing response has an unknown outcome. Reconnect
 * before another command so a late reply cannot acknowledge the next request. */
static bool exchange(unsigned channel, const uint8_t *packet, size_t length,
                     uint8_t response_id, uint32_t timeout)
{
    memset(&s_rx[channel], 0, sizeof(s_rx[channel]));
    if (!s_connected || esp_ble_gattc_write_char(s_interface, s_conn, s_write[channel],
            length, (uint8_t *)packet, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
        return false;
    bool written = false, replied = false;
    event_t event;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout);
    while (next_event(&event, deadline)) {
        if (event.kind == EV_WRITE && event.handle == s_write[channel] && event.conn == s_conn) {
            if (event.status != ESP_GATT_OK) return false;
            written = true;
        } else if (event.kind == EV_NOTIFY && event.handle == s_notify[channel] && event.conn == s_conn) {
            int result = gopro_rx_feed(&s_rx[channel], event.data, event.length);
            if (result < 0) return false;
            if (result == 1 && s_rx[channel].used >= 2 && s_rx[channel].data[0] == response_id)
                replied = true;
        }
        if (written && replied) return true;
    }
    return false;
}

static bool command(unsigned channel, const uint8_t *packet, size_t length)
{
    return exchange(channel, packet, length, packet[1], 3000) && s_rx[channel].data[1] == 0;
}

static void load_profile(void)
{
    nvs_handle_t nvs;
    size_t size = sizeof(s_address);
    if (nvs_open("gopro", NVS_READONLY, &nvs) != ESP_OK) return;
    s_remembered = nvs_get_blob(nvs, "address", s_address, &size) == ESP_OK &&
        size == sizeof(s_address) && nvs_get_u8(nvs, "type", &s_address_type) == ESP_OK;
    nvs_close(nvs);
}

static void save_profile(void)
{
    nvs_handle_t nvs;
    if (s_remembered) return;
    if (nvs_open("gopro", NVS_READWRITE, &nvs) != ESP_OK) return;
    esp_err_t result = nvs_set_blob(nvs, "address", s_address, sizeof(s_address));
    if (result == ESP_OK) result = nvs_set_u8(nvs, "type", s_address_type);
    if (result == ESP_OK) result = nvs_commit(nvs);
    nvs_close(nvs);
    s_remembered = result == ESP_OK;
    if (result != ESP_OK) ESP_LOGW(TAG, "Profile save failed: %s", esp_err_to_name(result));
}

static void disconnect_camera(void)
{
    phase(CAMERA_PHASE_RECONNECTING);
    /* Allow disconnect cleanup even when the event queue overflowed. */
    atomic_store(&s_overflow, false);
    if (s_connected) {
        esp_ble_gattc_close(s_interface, s_conn);
        event_t event;
        /* next_event consumes disconnect and invalidates the snapshot. */
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
        while (s_connected && next_event(&event, deadline)) {}
    }
    s_connected = false;
    for (unsigned i = 0; i < 4; ++i) {
        if (s_notify[i]) esp_ble_gattc_unregister_for_notify(s_interface, s_address, s_notify[i]);
        s_notify[i] = s_write[i] = 0;
    }
    memset(s_rx, 0, sizeof(s_rx));
}

static bool connect_camera(void)
{
    event_t event;
    phase(s_remembered ? CAMERA_PHASE_RECONNECTING : CAMERA_PHASE_SCANNING);
    atomic_store(&s_overflow, false);
    xQueueReset(s_events);
    if (esp_ble_gap_start_scanning(10) != ESP_OK) return false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10500);
    bool found = false;
    while (next_event(&event, deadline)) {
        if (atomic_load(&s_pair_requested)) break;
        if (event.kind == EV_SCAN_START && event.status != 0) break;
        if (event.kind != EV_SCAN_RESULT ||
            (s_remembered && memcmp(s_address, event.address, sizeof(s_address)) != 0)) continue;
        memcpy(s_address, event.address, sizeof(s_address));
        s_address_type = event.address_type;
        portENTER_CRITICAL(&s_mux);
        s_state.rssi = event.rssi;
        s_state.address_type = s_address_type;
        portEXIT_CRITICAL(&s_mux);
        found = true;
        break;
    }
    if (esp_ble_gap_stop_scanning() == ESP_OK) wait_event(EV_SCAN_STOP, 0, 1000, &event);
    if (!found) return false;
    phase(CAMERA_PHASE_VERIFYING);
    if (esp_ble_gattc_open(s_interface, s_address, s_address_type, true) != ESP_OK) return false;
    if (!wait_event(EV_OPEN, 0, 15000, &event)) {
        esp_ble_gattc_cancel_open_params_t cancel = {.gattc_if = s_interface};
        memcpy(cancel.remote_bda, s_address, sizeof(s_address));
        esp_ble_gattc_cancel_open(&cancel);
        esp_ble_gap_disconnect(s_address);
        return false;
    }
    s_conn = event.conn;
    s_connected = true;
    if (esp_ble_set_encryption(s_address, ESP_BLE_SEC_ENCRYPT) != ESP_OK ||
        !wait_event(EV_AUTH, 0, 20000, &event)) {
        phase(CAMERA_PHASE_PAIR_REJECTED);
        return false;
    }
    memset(s_service_start, 0, sizeof(s_service_start));
    memset(s_service_end, 0, sizeof(s_service_end));
    if (esp_ble_gattc_search_service(s_interface, s_conn, NULL) != ESP_OK ||
        !wait_event(EV_SEARCH_DONE, 0, 10000, &event)) return false;
    if (!s_service_start[0] || !s_service_start[1]) {
        phase(CAMERA_PHASE_GATT_MISMATCH); return false;
    }
    const uint16_t ids[] = {0x72, 0x74, 0x76, 0x91};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned service = i == 3 ? 1 : 0;
        for (unsigned response = 0; response < 2; ++response) {
            esp_gattc_char_elem_t characteristic;
            uint16_t count = 1;
            esp_bt_uuid_t uuid = gp_uuid(ids[i] + response);
            if (esp_ble_gattc_get_char_by_uuid(s_interface, s_conn, s_service_start[service],
                    s_service_end[service], uuid, &characteristic, &count) != ESP_GATT_OK ||
                count != 1 || !(characteristic.properties &
                    (response ? ESP_GATT_CHAR_PROP_BIT_NOTIFY : ESP_GATT_CHAR_PROP_BIT_WRITE))) {
                phase(CAMERA_PHASE_GATT_MISMATCH); return false;
            }
            if (response) s_notify[i] = characteristic.char_handle;
            else s_write[i] = characteristic.char_handle;
        }
        if (esp_ble_gattc_register_for_notify(s_interface, s_address, s_notify[i]) != ESP_OK ||
            !wait_event(EV_NOTIFY_REGISTER, s_notify[i], 3000, &event)) return false;
        esp_gattc_descr_elem_t descriptor;
        uint16_t count = 1;
        esp_bt_uuid_t cccd = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG};
        uint8_t enable[] = {1, 0};
        if (esp_ble_gattc_get_descr_by_char_handle(s_interface, s_conn, s_notify[i], cccd,
                &descriptor, &count) != ESP_GATT_OK || count != 1 ||
            esp_ble_gattc_write_char_descr(s_interface, s_conn, descriptor.handle, sizeof(enable),
                enable, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE) != ESP_OK ||
            !wait_event(EV_DESCRIPTOR, descriptor.handle, 3000, &event)) {
            phase(CAMERA_PHASE_SUBSCRIBE_FAILED); return false;
        }
    }
    return true;
}

static bool setup_camera(void)
{
    phase(CAMERA_PHASE_WAITING_STATUS);
    const uint8_t hardware[] = {1, 0x3c};
    bool ready = false;
    for (unsigned attempt = 0; attempt < 10 && s_connected; ++attempt) {
        if (!exchange(0, hardware, sizeof(hardware), 0x3c, 3000)) return false;
        if (s_rx[0].data[1] == 0) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!ready) return false;
    /* Open GoPro API version, then third-party client identification. */
    const uint8_t version[] = {1, 0x51};
    const uint8_t identify[] = {1, 0x50};
    if (!command(0, version, sizeof(version)) || s_rx[0].used != 6 ||
        s_rx[0].data[2] != 1 || s_rx[0].data[3] != 2 || s_rx[0].data[4] != 1) {
        ESP_LOGW(TAG, "Camera did not confirm Open GoPro API 2.x");
        return false;
    }
    if (!command(0, identify, sizeof(identify))) return false;
    /* RequestPairingFinish: result=SUCCESS(0), phoneName="RotorREC". */
    const uint8_t pairing[] = {14, 0x03, 0x01, 0x08, 0x00, 0x12, 8,
                              'R','o','t','o','r','R','E','C'};
    if (!exchange(3, pairing, sizeof(pairing), 0x03, 3000) || s_rx[3].used != 4 ||
        memcmp(s_rx[3].data, (uint8_t[]){0x03, 0x81, 0x08, 0x01}, 4) != 0) return false;
    return true;
}

static bool poll_status(void)
{
    const uint8_t query[] = {6, 0x13, 8, 10, 70, 82, 96};
    gopro_status_t status;
    if (!exchange(2, query, sizeof(query), 0x13, 3000) ||
        !gopro_parse_status(s_rx[2].data, s_rx[2].used, &status)) return false;
    portENTER_CRITICAL(&s_mux);
    s_status_tick = xTaskGetTickCount();
    s_state.phase = CAMERA_PHASE_READY;
    s_state.recording = status.encoding;
    /* A shutter in photo/timelapse mode is not a video recording request. */
    s_state.recording_valid = status.preset_group == GOPRO_PRESET_GROUP_VIDEO;
    s_state.saving = !status.encoding && (status.busy || !status.ready);
    s_state.battery_valid = true;
    s_state.snapshot = (camera_snapshot_t) {
        .valid = true, .recording = status.encoding, .battery = status.battery,
    };
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void gopro_get_state(camera_controller_state_t *state)
{
    if (!state) return;
    portENTER_CRITICAL(&s_mux);
    *state = s_state;
    if (xTaskGetTickCount() - s_status_tick >= pdMS_TO_TICKS(5000)) {
        state->recording_valid = false;
        state->battery_valid = false;
        state->snapshot.valid = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void finish_command(int result)
{
    portENTER_CRITICAL(&s_mux);
    s_state.last_command_result = result;
    s_state.command_pending = false;
    portEXIT_CRITICAL(&s_mux);
}

static void worker(void *argument)
{
    (void)argument;
    event_t event;
    if (!wait_event(EV_REGISTER, 0, 5000, &event)) goto failed;
    s_interface = event.interface;
    if (esp_ble_gap_set_scan_params(&s_scan) != ESP_OK ||
        !wait_event(EV_SCAN_PARAMS, 0, 5000, &event)) goto failed;
    load_profile();
    for (;;) {
        if (atomic_exchange(&s_pair_requested, false)) {
            nvs_handle_t nvs;
            if (nvs_open("gopro", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_erase_all(nvs); nvs_commit(nvs); nvs_close(nvs);
            }
            if (s_remembered) esp_ble_remove_bond_device(s_address);
            s_remembered = false;
        }
        if (connect_camera() && setup_camera() && poll_status()) {
            save_profile();
            ESP_LOGI(TAG, "Open GoPro session ready; select Video mode for record control");
            TickType_t last_keepalive = xTaskGetTickCount() - pdMS_TO_TICKS(2500);
            while (s_connected && !atomic_load(&s_pair_requested)) {
                bool target;
                if (xQueueReceive(s_commands, &target, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    camera_controller_state_t state;
                    gopro_get_state(&state);
                    if (!state.recording_valid || state.saving || state.phase != CAMERA_PHASE_READY) {
                        finish_command(-1);
                    } else if (target == state.recording) {
                        finish_command(0);
                    } else {
                        uint8_t shutter[] = {3, 1, 1, target ? 1 : 0};
                        portENTER_CRITICAL(&s_mux);
                        s_state.recording_valid = false;
                        portEXIT_CRITICAL(&s_mux);
                        bool received = exchange(0, shutter, sizeof(shutter), 1, 3000);
                        int result = received ? s_rx[0].data[1] : -3;
                        /* ACK never changes recording. Only a subsequent query can do so. */
                        if (!received || !poll_status()) {
                            phase(CAMERA_PHASE_RECONNECTING);
                            finish_command(result ? result : -3);
                            break;
                        }
                        /* Wait for the requested state, never resend the shutter. */
                        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
                        bool confirmed = result != 0;
                        while (!confirmed && s_connected) {
                            gopro_get_state(&state);
                            if (state.recording_valid && state.recording == target) {
                                confirmed = true;
                                break;
                            }
                            if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) break;
                            const uint8_t keepalive[] = {3, 0x5b, 1, 0x42};
                            if (!command(1, keepalive, sizeof(keepalive))) break;
                            last_keepalive = xTaskGetTickCount();
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            if (!poll_status()) break;
                        }
                        finish_command(confirmed ? result : -3);
                        if (!confirmed) break;
                    }
                }
                if (atomic_load(&s_pair_requested)) break;
                if (xTaskGetTickCount() - last_keepalive >= pdMS_TO_TICKS(2500)) {
                    const uint8_t keepalive[] = {3, 0x5b, 1, 0x42};
                    if (!command(1, keepalive, sizeof(keepalive))) break;
                    last_keepalive = xTaskGetTickCount();
                }
                if (!poll_status()) break;
            }
        }
        disconnect_camera();
        xQueueReset(s_commands);
        portENTER_CRITICAL(&s_mux);
        if (s_state.command_pending) s_state.last_command_result = -1;
        s_state.command_pending = false;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGW(TAG, "Session unavailable; retrying camera discovery");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
failed:
    phase(CAMERA_PHASE_SCAN_FAILED);
    ESP_LOGE(TAG, "BLE initialization failed");
    vTaskDelete(NULL);
}

esp_err_t gopro_init(void)
{
    s_state = (camera_controller_state_t) {
        .protocol = CAMERA_PROTOCOL_GOPRO, .phase = CAMERA_PHASE_INITIALIZING,
    };
    s_events = xQueueCreate(24, sizeof(event_t));
    s_commands = xQueueCreate(1, sizeof(bool));
    if (!s_events || !s_commands) return ESP_ERR_NO_MEM;
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result;
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((result = esp_bt_controller_init(&config)) != ESP_OK ||
        (result = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK ||
        (result = esp_bluedroid_init()) != ESP_OK ||
        (result = esp_bluedroid_enable()) != ESP_OK ||
        (result = esp_ble_gap_register_callback(gap_callback)) != ESP_OK ||
        (result = esp_ble_gattc_register_callback(gatt_callback)) != ESP_OK) return result;
    uint8_t auth = ESP_LE_AUTH_BOND, io = ESP_IO_CAP_NONE, key_size = 16;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    if ((result = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1)) != ESP_OK ||
        (result = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1)) != ESP_OK ||
        (result = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1)) != ESP_OK ||
        (result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1)) != ESP_OK ||
        (result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1)) != ESP_OK) return result;
    return esp_ble_gattc_app_register(1);
}

void gopro_start(void)
{
    if (!s_worker && xTaskCreate(worker, "gopro", 6144, NULL, 5, &s_worker) != pdPASS)
        phase(CAMERA_PHASE_SCAN_FAILED);
}

esp_err_t gopro_request_recording(bool recording)
{
    portENTER_CRITICAL(&s_mux);
    bool allowed = s_worker && s_state.phase == CAMERA_PHASE_READY && s_state.recording_valid &&
        !s_state.saving && !s_state.command_pending &&
        xTaskGetTickCount() - s_status_tick < pdMS_TO_TICKS(5000);
    if (allowed) s_state.command_pending = true;
    portEXIT_CRITICAL(&s_mux);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_commands, &recording, 0) != pdTRUE) {
        finish_command(-1);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void gopro_request_pairing(void)
{
    phase(CAMERA_PHASE_SCANNING);
    atomic_store(&s_pair_requested, true);
}
