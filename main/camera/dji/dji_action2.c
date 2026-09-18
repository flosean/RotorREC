#include "dji_action2.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "ble.h"
#include "connect_logic.h"

#define NVS_NAMESPACE "camlink"
#define MAC_KEY "a2_mac"
#define ADDRESS_TYPE_KEY "a2_type"
#define DUML_MAX_FRAME_LENGTH 64

static const char *TAG = "DJI_ACTION2";

typedef struct {
    uint16_t sequence;
    uint8_t flags;
    uint8_t command_set;
    uint8_t command_id;
    const uint8_t *payload;
    size_t payload_length;
} duml_frame_t;

static volatile dji_action2_stage_t s_stage;
static volatile bool s_recording;
static volatile bool s_record_pending;
static volatile bool s_record_target;
static volatile int s_last_result;
static volatile bool s_status_valid;
static volatile uint16_t s_record_time;
static volatile uint32_t s_remain_time;
static volatile uint32_t s_remain_capacity;
static volatile uint8_t s_shooting_mode;
static bool s_remembered;
static uint16_t s_sequence = 0x0100;
static uint8_t s_mac[ESP_BD_ADDR_LEN];
static uint8_t s_address_type;
static TaskHandle_t s_keepalive_task;
static uint32_t s_official_heartbeat_replies;
static uint32_t s_polled_status_replies;
static uint8_t s_polled_status_raw;
static volatile uint8_t s_sender = 0x02;
static volatile bool s_identity_probe_received;
static volatile bool s_identity_probe_waiting;
static uint8_t s_battery;
static bool s_battery_received;
static TickType_t s_battery_tick;
static bool s_record_state_received;
static bool s_saving;
static TickType_t s_record_state_tick;
static TickType_t s_record_command_tick;
static uint16_t s_record_command_sequence;

static bool record_status_known(uint8_t raw)
{
    return raw == 0x01 || raw == 0x81 || raw == 0xc1;
}

static bool battery_reply_valid(const uint8_t *payload, size_t length)
{
    return length >= 22 && payload[0] == 0 && payload[21] <= 100;
}

static bool recording_fresh(void)
{
    return s_stage == DJI_ACTION2_READY && s_record_state_received &&
        (TickType_t)(xTaskGetTickCount() - s_record_state_tick) < pdMS_TO_TICKS(5000);
}
static dji_action2_disconnect_callback_t s_disconnect_callback;

static void notification_handler(const uint8_t *data, size_t length);

static uint8_t duml_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x77;
    while (length-- > 0) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (uint8_t)((crc >> 1) ^ 0x8c) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

static uint16_t duml_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x3692;
    while (length-- > 0) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static size_t duml_build(uint8_t *output, size_t capacity,
                         uint8_t sender, uint8_t receiver, uint16_t sequence,
                         uint8_t flags, uint8_t command_set, uint8_t command_id,
                         const uint8_t *payload, size_t payload_length)
{
    size_t total_length = 13 + payload_length;
    if (output == NULL || total_length > capacity || total_length > 0x3ff ||
        (payload_length > 0 && payload == NULL)) {
        return 0;
    }

    output[0] = 0x55;
    output[1] = total_length & 0xff;
    output[2] = 0x04 | ((total_length >> 8) & 0x03);
    output[3] = duml_crc8(output, 3);
    output[4] = sender;
    output[5] = receiver;
    output[6] = sequence >> 8;
    output[7] = sequence & 0xff;
    output[8] = flags;
    output[9] = command_set;
    output[10] = command_id;
    if (payload_length > 0) {
        memcpy(output + 11, payload, payload_length);
    }
    uint16_t crc = duml_crc16(output, total_length - 2);
    output[total_length - 2] = crc & 0xff;
    output[total_length - 1] = crc >> 8;
    return total_length;
}

static bool duml_parse(const uint8_t *data, size_t length, duml_frame_t *frame)
{
    if (data == NULL || frame == NULL || length < 13 || data[0] != 0x55) {
        return false;
    }
    size_t frame_length = data[1] | ((size_t)(data[2] & 0x03) << 8);
    if (frame_length < 13 || frame_length > length || (data[2] >> 2) != 1) {
        return false;
    }
    uint16_t received_crc = data[frame_length - 2] | ((uint16_t)data[frame_length - 1] << 8);
    if (data[3] != duml_crc8(data, 3) || received_crc != duml_crc16(data, frame_length - 2)) {
        return false;
    }

    *frame = (duml_frame_t) {
        .sequence = ((uint16_t)data[6] << 8) | data[7],
        .flags = data[8],
        .command_set = data[9],
        .command_id = data[10],
        .payload = data + 11,
        .payload_length = frame_length - 13,
    };
    return true;
}

static esp_err_t send_duml_with_sequence(uint8_t receiver, uint16_t sequence,
                                         uint8_t flags, uint8_t command_set, uint8_t command_id,
                                         const uint8_t *payload, size_t payload_length)
{
    uint8_t frame[DUML_MAX_FRAME_LENGTH];
    size_t length = duml_build(frame, sizeof(frame), s_sender, receiver, sequence,
                               flags, command_set, command_id, payload, payload_length);
    if (length == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ble_write_without_response(s_ble_profile.conn_id, s_ble_profile.write_char_handle,
                                      frame, length);
}

static esp_err_t send_duml(uint8_t receiver, uint8_t flags, uint8_t command_set,
                           uint8_t command_id, const uint8_t *payload, size_t payload_length)
{
    return send_duml_with_sequence(receiver, s_sequence++, flags, command_set, command_id,
                                   payload, payload_length);
}

static bool duml_self_test(void)
{
    static const uint8_t payload[] = {
        0x0f, '0', '0', '1', '7', '4', '9', '3', '1', '9', '2', '8', '6', '1', '0', '2',
        0x04, '5', '1', '6', '0',
    };
    static const uint8_t expected[] = {
        0x55, 0x22, 0x04, 0xea, 0x02, 0x07, 0x80, 0x92,
        0x40, 0x07, 0x45, 0x0f, 0x30, 0x30, 0x31, 0x37,
        0x34, 0x39, 0x33, 0x31, 0x39, 0x32, 0x38, 0x36,
        0x31, 0x30, 0x32, 0x04, 0x35, 0x31, 0x36, 0x30,
        0x2e, 0x42,
    };
    uint8_t frame[sizeof(expected)];
    duml_frame_t parsed;
    bool pairing_ok = duml_build(frame, sizeof(frame), 0x02, 0x07, 0x8092,
                      0x40, 0x07, 0x45, payload, sizeof(payload)) == sizeof(expected) &&
           memcmp(frame, expected, sizeof(expected)) == 0 && duml_parse(frame, sizeof(frame), &parsed) &&
           parsed.sequence == 0x8092 && parsed.payload_length == sizeof(payload);
    // Independent on-air fixture: official remote 02/70 query, capture frame 5840.
    static const uint8_t captured_query[] = {
        0x55, 0x0e, 0x04, 0x66, 0x53, 0x01, 0x04, 0x00,
        0x20, 0x02, 0x70, 0x01, 0x02, 0xb1,
    };
    static const uint8_t query[] = {0x01};
    uint8_t battery_fixture[22] = {0};
    battery_fixture[21] = 40;
    bool status_ok = record_status_known(0x01) && record_status_known(0x81) &&
        record_status_known(0xc1) && !record_status_known(0xff) &&
        battery_reply_valid(battery_fixture, sizeof(battery_fixture)) &&
        !battery_reply_valid(battery_fixture, 21);
    battery_fixture[21] = 101;
    status_ok = status_ok && !battery_reply_valid(battery_fixture, sizeof(battery_fixture));
    return pairing_ok && status_ok &&
        duml_build(frame, sizeof(frame), 0x53, 0x01, 0x0400,
                   0x20, 0x02, 0x70, query, sizeof(query)) == sizeof(captured_query) &&
        memcmp(frame, captured_query, sizeof(captured_query)) == 0 &&
        duml_parse(captured_query, sizeof(captured_query), &parsed) &&
        parsed.command_set == 0x02 && parsed.command_id == 0x70 &&
        parsed.payload_length == 1 && parsed.payload[0] == 0x01;
}

static void remember_profile(void)
{
    ble_diagnostics_t diagnostics;
    nvs_handle_t handle;
    ble_get_diagnostics(&diagnostics);

    if (s_remembered && memcmp(s_mac, s_ble_profile.remote_bda, sizeof(s_mac)) == 0 &&
        s_address_type == diagnostics.address_type) {
        return;
    }

    memcpy(s_mac, s_ble_profile.remote_bda, sizeof(s_mac));
    s_address_type = diagnostics.address_type;
    s_remembered = true;
    ble_set_reconnect_target(s_mac, s_address_type);

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not open Action 2 profile storage");
        return;
    }
    esp_err_t result = nvs_set_blob(handle, MAC_KEY, s_mac, sizeof(s_mac));
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, ADDRESS_TYPE_KEY, s_address_type);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not save Action 2 profile: %s", esp_err_to_name(result));
    }
}

static void keepalive_task(void *argument)
{
    static const uint8_t keepalive[] = {0x01, 0x01};
    static const uint8_t status_query[] = {0x01};
    unsigned cycle = 0;
    (void)argument;

    while (s_stage == DJI_ACTION2_READY && connect_logic_get_state() == BLE_CONNECTED) {
        if (s_record_pending && (TickType_t)(xTaskGetTickCount() - s_record_command_tick) >= pdMS_TO_TICKS(3000)) {
            s_record_pending = false;
            s_last_result = -3;
            ESP_LOGW(TAG, "Record ACK timeout; no automatic retry");
        }
        if ((cycle++ % 2) == 0) {
            send_duml(0xf0, 0x40, 0x00, 0x2b, keepalive, sizeof(keepalive));
            // Captured BTC01 uses 53 -> 28, 00/00 once per second.
            // Uses the sender selected during the experimental 07/19 handshake.
            esp_err_t result = send_duml(0x28, 0x00, 0x00, 0x00, NULL, 0);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Official heartbeat probe send failed: %s", esp_err_to_name(result));
            }
        }
        esp_err_t result = send_duml(0x01, 0x20, 0x02, 0x70,
                                     status_query, sizeof(status_query));
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "02/70 status query send failed: %s", esp_err_to_name(result));
        }
        if ((cycle % 4) == 0) {
            static const uint8_t battery_query[] = {0x00, 0x00, 0x00, 0x00};
            result = send_duml(0x05, 0x20, 0x0d, 0x02, battery_query, sizeof(battery_query));
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "0D/02 battery query send failed: %s", esp_err_to_name(result));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_keepalive_task = NULL;
    vTaskDelete(NULL);
}

static void pair_complete(void)
{
    s_stage = DJI_ACTION2_READY;
    remember_profile();
    if (s_keepalive_task == NULL) {
        xTaskCreate(keepalive_task, "action2_keepalive", 3072, NULL, 4, &s_keepalive_task);
    }
}

static void disconnect_handler(void)
{
    ESP_LOGI(TAG, "Session ended: heartbeat replies=%lu status replies=%lu",
             (unsigned long)s_official_heartbeat_replies, (unsigned long)s_polled_status_replies);
    s_stage = DJI_ACTION2_IDLE;
    s_record_pending = false;
    if (s_disconnect_callback != NULL) {
        s_disconnect_callback();
    }
}

static void notification_handler(const uint8_t *data, size_t length)
{
    duml_frame_t frame;
    if (!duml_parse(data, length, &frame)) {
        if (length > 0 && data[0] == 0x55) {
            ESP_LOGW(TAG, "Ignored invalid DUML frame len=%u", (unsigned)length);
        }
        return;
    }

    if (frame.flags == 0xc0 && frame.command_set == 0x07 && frame.command_id == 0x19 &&
        data[4] == 0x07 && data[5] == 0x02 && s_identity_probe_waiting) {
        ESP_LOGI(TAG, "07/19 identity probe reply len=%u",
                 (unsigned)frame.payload_length);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame.payload, frame.payload_length, ESP_LOG_INFO);
        static const uint8_t observed_reply[] = {0x00, 0xff, 0x55, 0x53, 0x00};
        if (frame.payload_length == sizeof(observed_reply) &&
            memcmp(frame.payload, observed_reply, sizeof(observed_reply)) == 0) {
            s_sender = 0x53;
            ESP_LOGI(TAG, "Experimental remote sender selected: 0x53");
        }
        s_identity_probe_received = true;
    } else if (frame.flags == 0xc0 && frame.command_set == 0x0d && frame.command_id == 0x02 &&
               data[4] == 0x05 && data[5] == s_sender) {
        // Query reply has one result byte before the battery status structure.
        if (battery_reply_valid(frame.payload, frame.payload_length)) {
            if (!s_battery_received || s_battery != frame.payload[21]) {
                ESP_LOGI(TAG, "Camera battery=%u%%", frame.payload[21]);
            }
            s_battery = frame.payload[21];
            s_battery_tick = xTaskGetTickCount();
            s_battery_received = true;
        }
    } else if (frame.flags == 0xc0 && frame.command_set == 0x02 && frame.command_id == 0x19 &&
               data[4] == 0x01 && data[5] == s_sender) {
        ESP_LOGI(TAG, "02/19 candidate battery reply len=%u (unverified layout)",
                 (unsigned)frame.payload_length);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame.payload, frame.payload_length, ESP_LOG_INFO);
    } else if (frame.flags == 0xc0 && frame.command_set == 0x00 && frame.command_id == 0x00 &&
        frame.payload_length >= 1 && data[4] == 0x28 && data[5] == s_sender) {
        ++s_official_heartbeat_replies;
        if (s_official_heartbeat_replies == 1 || frame.payload[0] != 0) {
            ESP_LOGI(TAG, "Official heartbeat probe reply: result=0x%02x", frame.payload[0]);
        }
    } else if (frame.flags == 0xc0 && frame.command_set == 0x02 && frame.command_id == 0x70 &&
               frame.payload_length >= 6 && frame.payload[0] == 0 &&
               data[4] == 0x01 && data[5] == s_sender) {
        ++s_polled_status_replies;
        s_record_state_received = record_status_known(frame.payload[1]);
        s_record_state_tick = xTaskGetTickCount();
        if (s_record_state_received) {
            s_saving = frame.payload[1] == 0xc1;
            s_recording = frame.payload[1] == 0x81;
        }
        if (s_polled_status_replies == 1 || s_polled_status_raw != frame.payload[1]) {
            s_polled_status_raw = frame.payload[1];
            ESP_LOGI(TAG, "02/70 raw status=%02x %02x %02x %02x %02x",
                     frame.payload[1], frame.payload[2], frame.payload[3],
                     frame.payload[4], frame.payload[5]);
        }
    } else if (frame.flags == 0x40 && frame.command_set == 0x07 && frame.command_id == 0x46 &&
        frame.payload_length >= 1 && frame.payload[0] == 0x01) {
        send_duml_with_sequence(0x07, frame.sequence, 0xc0, 0x07, 0x46,
                                frame.payload, 1);
        ESP_LOGI(TAG, "Pairing approved and acknowledged");
        pair_complete();
    } else if (frame.flags == 0xc0 && frame.command_set == 0x07 && frame.command_id == 0x45 &&
               frame.payload_length >= 2 && frame.payload[1] == 0x01) {
        ESP_LOGI(TAG, "Previously paired session accepted");
        pair_complete();
    } else if (frame.flags == 0xc0 && frame.command_set == 0x02 && frame.command_id == 0x02 &&
               frame.payload_length >= 1 && s_record_pending &&
               frame.sequence == s_record_command_sequence &&
               data[4] == 0x01 && data[5] == s_sender) {
        s_last_result = frame.payload[0];
        /* ACK confirms the command, not the recording state. 02/70 supplies that. */
        if (frame.payload[0] == 0x00) s_record_state_received = false;
        s_record_pending = false;
        ESP_LOGI(TAG, "Record command ACK: target=%s result=0x%02x; awaiting camera status",
                 s_record_target ? "ON" : "OFF", frame.payload[0]);
    } else if (frame.flags == 0x00 && frame.command_set == 0x02 && frame.command_id == 0x80 &&
               frame.payload_length >= 31) {
        bool recording = (frame.payload[0] & 0x80) != 0;
        uint32_t remain_capacity = (uint32_t)frame.payload[9] |
            ((uint32_t)frame.payload[10] << 8) | ((uint32_t)frame.payload[11] << 16) |
            ((uint32_t)frame.payload[12] << 24);
        uint32_t remain_time = frame.payload[17] | ((uint32_t)frame.payload[18] << 8);
        uint16_t record_time = frame.payload[29] | ((uint16_t)frame.payload[30] << 8);
        uint8_t shooting_mode = frame.payload_length > 57 ? frame.payload[57] : 0xff;
        bool changed = !s_status_valid || recording != s_recording || record_time != s_record_time ||
            remain_capacity != s_remain_capacity || shooting_mode != s_shooting_mode;

        s_status_valid = true;
        /* Unverified 02/80 layout is diagnostic only; control/OSD use confirmed 02/70. */
        s_record_time = record_time;
        s_remain_time = remain_time;
        s_remain_capacity = remain_capacity;
        s_shooting_mode = shooting_mode;
        if (changed) {
            ESP_LOGI(TAG, "Unverified 02/80 candidate rec=%u elapsed=%us remain=%lus sd=%luMB mode=0x%02x len=%u",
                     recording, record_time, (unsigned long)remain_time,
                     (unsigned long)remain_capacity, shooting_mode,
                     (unsigned)frame.payload_length);
        }
    }
}

void dji_action2_init(dji_action2_disconnect_callback_t disconnect_callback)
{
    if (!duml_self_test()) {
        ESP_LOGE(TAG, "DUML codec self-test failed");
        abort();
    }
    ESP_LOGI(TAG, "DUML codec self-test passed");
    s_disconnect_callback = disconnect_callback;
    dji_action2_reset();
}

void dji_action2_load_profile(void)
{
    nvs_handle_t handle;
    size_t mac_size = sizeof(s_mac);
    uint8_t address_type = 0;

    s_remembered = false;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    esp_err_t mac_result = nvs_get_blob(handle, MAC_KEY, s_mac, &mac_size);
    esp_err_t type_result = nvs_get_u8(handle, ADDRESS_TYPE_KEY, &address_type);
    nvs_close(handle);

    if (mac_result == ESP_OK && mac_size == sizeof(s_mac) && type_result == ESP_OK) {
        s_address_type = address_type;
        s_remembered = true;
        ble_set_reconnect_target(s_mac, s_address_type);
        ESP_LOGI(TAG, "Remembered %02X:%02X:%02X:%02X:%02X:%02X type=%u",
                 s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5], s_address_type);
    }
}

void dji_action2_forget_profile(void)
{
    nvs_handle_t handle;
    s_remembered = false;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, MAC_KEY);
        nvs_erase_key(handle, ADDRESS_TYPE_KEY);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool dji_action2_get_profile(uint8_t address[ESP_BD_ADDR_LEN], uint8_t *address_type)
{
    if (!s_remembered) {
        return false;
    }
    if (address != NULL) {
        memcpy(address, s_mac, sizeof(s_mac));
    }
    if (address_type != NULL) {
        *address_type = s_address_type;
    }
    return true;
}

void dji_action2_reset(void)
{
    s_stage = DJI_ACTION2_IDLE;
    s_recording = false;
    s_record_pending = false;
    s_record_target = false;
    s_last_result = 0;
    s_status_valid = false;
    s_record_time = 0;
    s_remain_time = 0;
    s_remain_capacity = 0;
    s_shooting_mode = 0xff;
    s_sequence = 0x0100;
    s_sender = 0x02;
    s_identity_probe_waiting = false;
    s_identity_probe_received = false;
    s_battery_received = false;
    s_record_state_received = false;
    s_saving = false;
}

void dji_action2_detach(void)
{
    ble_set_notify_callback(NULL);
    ble_set_state_callback(NULL);
    s_stage = DJI_ACTION2_IDLE;
    s_record_pending = false;
}

esp_err_t dji_action2_trigger_pairing(void)
{
    static const uint8_t trigger[] = {0x01, 0x00};
    static const uint8_t session_wake[] = {0x04, 0x00};
    static const uint8_t pair_request[] = {
        0x0f, '0', '0', '1', '7', '4', '9', '3', '1', '9', '2', '8', '6', '1', '0', '2',
        0x04, '5', '1', '6', '0',
    };

    if (connect_logic_get_state() != BLE_CONNECTED ||
        s_ble_profile.notify_char_handle == 0 || s_ble_profile.write_char_handle == 0) {
        s_last_result = -1;
        s_stage = DJI_ACTION2_REJECTED;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ble_write_with_response(s_ble_profile.conn_id,
                                                s_ble_profile.notify_char_handle,
                                                trigger, sizeof(trigger));
    vTaskDelay(pdMS_TO_TICKS(200));
    if (result == ESP_OK) {
        // Official capture starts with 02 -> 07, flags 40, 07/19, empty payload.
        // Controlled experiment: select the observed official sender only for
        // the exact captured reply. Unknown/absent replies retain sender 02.
        s_sender = 0x02;
        s_identity_probe_received = false;
        s_identity_probe_waiting = true;
        esp_err_t probe_result = send_duml(0x07, 0x40, 0x07, 0x19, NULL, 0);
        ESP_LOGI(TAG, "07/19 identity probe sent: %s", esp_err_to_name(probe_result));
        for (unsigned attempt = 0; probe_result == ESP_OK &&
             !s_identity_probe_received && attempt < 20 &&
             connect_logic_get_state() == BLE_CONNECTED; ++attempt) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_identity_probe_waiting = false;
        ESP_LOGI(TAG, "Pairing with sender=0x%02x (identifier unchanged)", s_sender);
    }
    if (result == ESP_OK) {
        result = send_duml(0xf0, 0x40, 0x00, 0x2b, session_wake, sizeof(session_wake));
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    if (result == ESP_OK) {
        result = send_duml(0x07, 0x40, 0x07, 0x45, pair_request, sizeof(pair_request));
    }
    s_stage = result == ESP_OK ? DJI_ACTION2_WAITING_APPROVAL : DJI_ACTION2_REJECTED;
    ESP_LOGI(TAG, "Session pairing request: %s", esp_err_to_name(result));
    return result;
}

esp_err_t dji_action2_begin_session(void)
{
    s_battery_received = false;
    s_record_state_received = false;
    s_saving = false;
    s_official_heartbeat_replies = 0;
    s_polled_status_replies = 0;
    s_polled_status_raw = 0;
    s_stage = DJI_ACTION2_SESSION_SETUP;
    s_record_pending = false;
    ble_set_notify_callback(notification_handler);
    ble_set_state_callback(disconnect_handler);

    if (s_ble_profile.read_char_handle != 0) {
        ble_register_notify(s_ble_profile.conn_id, s_ble_profile.read_char_handle);
        ble_read(s_ble_profile.conn_id, s_ble_profile.read_char_handle);
    }
    ble_read(s_ble_profile.conn_id, s_ble_profile.notify_char_handle);
    ble_register_notify(s_ble_profile.conn_id, s_ble_profile.write_char_handle);
    ble_read(s_ble_profile.conn_id, s_ble_profile.write_char_handle);
    return dji_action2_trigger_pairing();
}

esp_err_t dji_action2_toggle_recording(void)
{
    return dji_action2_set_recording(!s_recording);
}

esp_err_t dji_action2_set_recording(bool start)
{
    if (s_stage != DJI_ACTION2_READY || s_record_pending || !recording_fresh() || s_saving) {
        s_last_result = -1;
        return ESP_ERR_INVALID_STATE;
    }

    if (s_recording == start) return ESP_OK;
    uint8_t payload = start ? 0x01 : 0x00;
    s_record_target = start;
    s_record_command_sequence = s_sequence++;
    s_record_command_tick = xTaskGetTickCount();
    s_record_pending = true;
    s_record_state_received = false;
    s_last_result = 0;
    esp_err_t result = send_duml_with_sequence(0x01, s_record_command_sequence,
                                              0x40, 0x02, 0x02, &payload, sizeof(payload));
    if (result != ESP_OK) {
        s_record_pending = false;
        s_last_result = -2;
    }
    ESP_LOGI(TAG, "Record %s: %s", start ? "START" : "STOP", esp_err_to_name(result));
    return result;
}

bool dji_action2_is_ready(void)
{
    return s_stage == DJI_ACTION2_READY;
}

void dji_action2_get_state(dji_action2_state_t *state)
{
    if (state == NULL) {
        return;
    }
    ble_diagnostics_t diagnostics;
    ble_get_diagnostics(&diagnostics);
    *state = (dji_action2_state_t) {
        .stage = s_stage,
        .remembered = s_remembered,
        .recording = s_recording,
        .recording_valid = recording_fresh(),
        .saving = s_saving,
        .battery = s_battery,
        .battery_valid = s_stage == DJI_ACTION2_READY && s_battery_received &&
            (TickType_t)(xTaskGetTickCount() - s_battery_tick) < pdMS_TO_TICKS(10000),
        .command_pending = s_record_pending,
        .last_command_result = s_last_result,
        .rssi = diagnostics.rssi,
        .address_type = diagnostics.address_type,
        .mtu = diagnostics.mtu,
        .gatt_attribute_count = diagnostics.attribute_count,
        .notification_count = diagnostics.notification_count,
        .target_service_found = diagnostics.target_service_found,
    };
}
