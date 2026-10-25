#include "bf_bridge.h"
#include "management/management.h"

#include <string.h>
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "camera/camera_controller.h"
#include "bf_control.h"
#include "msp_v2.h"
#include "osd_messages.h"

static const char *TAG = "BF_BRIDGE";
#define BF_UART UART_NUM_1
#define BF_RESPONSE_MS 150

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* The only FC write permitted is volatile OSD text. No RC, ARM, config or EEPROM writes. */
static bool command_allowed(uint16_t command)
{
    return command == MSP_API_VERSION || command == MSP_FC_VARIANT ||
        command == MSP_BOXIDS || command == MSP_STATUS || command == MSP_V2_SET_TEXT;
}

static bool exchange(uint16_t command, const uint8_t *frame, size_t length, msp_v2_reply_t *reply)
{
    if (management_active() || !command_allowed(command) || length == 0) return false;
    if (uart_write_bytes(BF_UART, frame, length) != (int)length) return false;
    msp_v2_parser_t parser = {0};
    uint32_t start = now_ms();
    while ((uint32_t)(now_ms() - start) < BF_RESPONSE_MS) {
        uint8_t data[64];
        int count = uart_read_bytes(BF_UART, data, sizeof(data), pdMS_TO_TICKS(10));
        bool found = false;
        for (int i = 0; i < count; ++i) {
            management_feed(data[i]);
            if (!found && msp_v2_parser_feed(&parser, data[i], reply) && reply->command == command) found = true;
        }
        management_tick();
        if (management_active()) return false;
        if (found) return true;
    }
    return false;
}

/* One UART reader for both MSP and management, including disconnected waits. */
static void service_for(uint32_t duration_ms)
{
    uint32_t start = now_ms();
    do {
        uint8_t data[128];
        int count = uart_read_bytes(BF_UART, data, sizeof(data), pdMS_TO_TICKS(10));
        for (int i = 0; i < count; ++i) management_feed(data[i]);
        management_tick();
    } while ((uint32_t)(now_ms() - start) < duration_ms);
}

static bool query(uint16_t command, msp_v2_reply_t *reply)
{
    uint8_t frame[9];
    size_t length = msp_v2_build_request(command, NULL, 0, frame, sizeof(frame));
    return exchange(command, frame, length, reply) && !reply->error;
}

static bool query_mode_page(uint8_t page, uint8_t ids[32], size_t *count)
{
    uint8_t frame[10];
    msp_v2_reply_t reply;
    size_t length = msp_v2_build_request(MSP_BOXIDS, &page, 1, frame, sizeof(frame));
    if (!exchange(MSP_BOXIDS, frame, length, &reply) || reply.error || reply.length > 32) return false;
    *count = reply.length;
    memcpy(ids, reply.payload, *count);
    return true;
}

static void bridge_task(void *argument)
{
    (void)argument;
    bool connected = false;
    bool map_valid = false;
    bool osd_supported = true;
    const rr_settings_t *settings = management_settings();
    unsigned osd_count = settings->owns_extra_lines ? 4 : 2;
    bool osd_acked[4] = {0};
    bool osd_attempted[4] = {0};
    uint32_t osd_attempt_ms[4] = {0};
    char osd_sent[4][17] = {{0}};
    bf_osd_state_t osd_state = {0};
    bf_mode_map_t map = { .mode_bit = -1, .failsafe_bit = -1 };
    bf_mode_map_t ground_map = {.mode_bit = -1, .failsafe_bit = -1, .arm_bit = -1};
    bool ground_map_valid = false;
    bf_automation_t policy = {0};
    uint32_t manual_sequence = 0;
    bool restore_stop = true;
    msp_v2_reply_t reply;
    uint32_t map_ms = 0;
    unsigned failures = 0;
    bool last_input_valid = false;

    for (;;) {
        uint32_t cycle_start = now_ms();
        if (management_take_control_reset()) {
            connected = false; map_valid = false; last_input_valid = false;
            policy = (bf_automation_t){0};
            camera_controller_stop_countdown(0, 0);
            restore_stop = true;
        }
        if (management_active()) {
            connected = false; map_valid = false; last_input_valid = false;
            policy = (bf_automation_t){0};
            camera_controller_stop_countdown(0, 0);
            restore_stop = true;
            service_for(20);
            continue;
        }
        if (restore_stop) {
            bool pending;
            uint8_t remaining;
            esp_err_t saved = management_stop_load(&pending, &remaining);
            if (saved == ESP_OK) {
                if (pending) bf_automation_restore_stop(&policy, now_ms(), remaining, settings);
                restore_stop = false;
            } else ESP_LOGW(TAG, "Could not load pending stop: %s", esp_err_to_name(saved));
        }
        if (!connected) {
            /* Keep legitimate queued intents across UART reconnection. The
             * policy suppresses commands until valid FC state returns. */
            if (!query(MSP_API_VERSION, &reply) || !bf_api_supported(reply.payload, reply.length)) {
                ESP_LOGW(TAG, "Waiting for MSP API 1.47+ on UART1 (no camera automation)");
                service_for(2000);
                continue;
            }
            unsigned api_minor = reply.payload[2];
            if (!query(MSP_FC_VARIANT, &reply) || reply.length != 4 ||
                memcmp(reply.payload, "BTFL", 4) != 0) {
                ESP_LOGW(TAG, "FC variant is unavailable or not Betaflight; control disabled");
                service_for(2000);
                continue;
            }
            connected = true;
            management_fc_state(false, false, false);
            failures = 0;
            osd_supported = true;
            memset(osd_acked, 0, sizeof(osd_acked));
            memset(osd_attempted, 0, sizeof(osd_attempted));
            map_ms = now_ms() - 5000;
            ESP_LOGI(TAG, "Betaflight API 1.%u connected; recording mode=%u, USER%u input", api_minor,
                     (unsigned)settings->record_mode, (unsigned)(settings->user_mode_id - 39));
        }

        if ((uint32_t)(now_ms() - map_ms) >= 5000) {
            bf_mode_map_t next_map;
            uint8_t selected = settings->record_mode == RR_RECORD_ARM || settings->record_mode == RR_RECORD_OFF
                ? 0 : settings->user_mode_id;
            bool next_valid = bf_mode_map_read(&next_map, selected, query_mode_page);
            if (!next_valid || !map_valid || next_map.mode_bit != map.mode_bit ||
                next_map.failsafe_bit != map.failsafe_bit || next_map.arm_bit != map.arm_bit) {
                policy.user = (bf_record_policy_t){0};
                policy.arm_sample_valid = false;
            }
            map_valid = next_valid;
            if (map_valid) map = next_map;
            else ESP_LOGW(TAG, "Selected control / FAILSAFE map unavailable; OSD only");
            /* Maintenance must observe ARM even if the selected USER is absent. */
            if (map_valid && map.arm_bit >= 0) {
                ground_map = map; ground_map.mode_bit = map.arm_bit; ground_map_valid = true;
            } else ground_map_valid = bf_mode_map_read(&ground_map, 0, query_mode_page);
            map_ms = now_ms();
        }

        bool status_ok = query(MSP_STATUS, &reply);
        bf_mode_status_t mode = {0};
        bool input_valid = !settings->link_paused && status_ok && map_valid &&
            bf_mode_status_parse(&map, reply.payload, reply.length, &mode) && !mode.failsafe;
        bf_mode_status_t ground = {0};
        bool ground_valid = status_ok && ground_map_valid &&
            bf_mode_status_parse(&ground_map, reply.payload, reply.length, &ground) && ground.arm_valid;
        management_fc_state(ground_valid, ground.armed, ground.failsafe);
        camera_controller_state_t camera;
        camera_controller_get_state(&camera);
        if (camera.manual_command_sequence != manual_sequence) {
            manual_sequence = camera.manual_command_sequence;
            bf_automation_manual(&policy, now_ms(), camera.manual_record_target);
        }
        bf_record_action_t action = bf_automation_step(&policy, now_ms(), input_valid, &mode, settings, &camera);
        camera.stop_delay_remaining = bf_stop_delay_remaining(&policy, now_ms(), settings);
        camera_controller_stop_countdown(camera.stop_delay_remaining, manual_sequence);
        if (!restore_stop) {
            esp_err_t saved = camera_controller_persist_stop(policy.intent_valid && !policy.target,
                                                             camera.stop_delay_remaining, manual_sequence);
            if (saved != ESP_OK) ESP_LOGW(TAG, "Pending stop persistence failed: %s", esp_err_to_name(saved));
        }
        if (input_valid != last_input_valid) {
            ESP_LOGI(TAG, "Control input %s%s", input_valid ? "valid" : "inhibited",
                     input_valid ? (settings->record_mode == RR_RECORD_ARM ? "; ARM state ready" :
                         settings->record_mode == RR_RECORD_OFF ? "; monitoring only" : "; set USER input LOW before use")
                         : "; camera recording is unchanged");
            last_input_valid = input_valid;
        }
        if (action != BF_RECORD_NONE) {
            esp_err_t result = camera_controller_automate_recording(action == BF_RECORD_START, manual_sequence);
            ESP_LOGI(TAG, "Flight control -> camera %s: %s (watch actual OSD status)",
                     action == BF_RECORD_START ? "START" : "STOP", esp_err_to_name(result));
        }
        if (!status_ok) {
            if (++failures >= 3) {
                connected = false;
                map_valid = false;
                ESP_LOGW(TAG, "MSP link lost; re-handshaking, never stopping camera on UART loss");
            }
        } else {
            failures = 0;
        }

        if (connected && status_ok && osd_supported) {
            char desired[4][17];
            bf_camera_osd_layout(&camera, settings, &osd_state, management_osd_layout(), desired);
            for (unsigned line = 0; line < osd_count; ++line) {
                if (osd_acked[line] && strcmp(desired[line], osd_sent[line]) == 0 &&
                    (uint32_t)(now_ms() - osd_attempt_ms[line]) < 5000) continue;
                if (osd_attempted[line] && (uint32_t)(now_ms() - osd_attempt_ms[line]) < 500) continue;
                uint8_t frame[BETAFLIGHT_OSD_MAX_FRAME_LENGTH];
                size_t length = betaflight_osd_build_custom_message(line, desired[line], frame, sizeof(frame));
                osd_attempted[line] = true;
                osd_attempt_ms[line] = now_ms();
                /* A lost ACK may still mean FC applied the text. Never trust the old cache. */
                osd_acked[line] = false;
                if (exchange(MSP_V2_SET_TEXT, frame, length, &reply)) {
                    if (reply.error) {
                        osd_supported = false;
                        ESP_LOGW(TAG, "FC rejected custom OSD text; camera control remains available");
                        break;
                    }
                    memcpy(osd_sent[line], desired[line], sizeof(osd_sent[line]));
                    osd_acked[line] = true;
                }
            }
        }
        uint32_t elapsed = now_ms() - cycle_start;
        service_for(elapsed < 100 ? 100 - elapsed : 10);
    }
}

static bool board_uart_pin_safe(int pin)
{
#if CONFIG_IDF_TARGET_ESP32C3
    /* C3-Zero: avoid straps 2/8/9, RGB 10, flash 12..17, USB 18/19, unexposed 11. */
    return pin == 0 || pin == 1 || pin == 3 || pin == 4 || pin == 5 || pin == 6 ||
        pin == 7 || pin == 20 || pin == 21;
#else
    /* Exclude LCD, SD, RGB LED, BOOT, USB and strap pins on Waveshare C6 LCD 1.47. */
    return pin == 0 || pin == 1 || pin == 2 || pin == 3 || pin == 10 || pin == 11 ||
        pin == 16 || pin == 17 || pin == 18 || pin == 19 || pin == 20;
#endif
}

esp_err_t betaflight_bridge_start(void)
{
#ifndef CONFIG_BF_CAM_UART_ENABLED
    return ESP_OK;
#endif
    static bool started;
    if (started) return ESP_ERR_INVALID_STATE;
    int tx = CONFIG_BF_CAM_UART_TX_GPIO;
    int rx = CONFIG_BF_CAM_UART_RX_GPIO;
    if (tx == rx || !board_uart_pin_safe(tx) || !board_uart_pin_safe(rx) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(tx) || !GPIO_IS_VALID_GPIO(rx)) return ESP_ERR_INVALID_ARG;
    uart_config_t config = {
        .baud_rate = CONFIG_BF_CAM_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_param_config(BF_UART, &config);
    if (result != ESP_OK) return result;
    result = uart_set_pin(BF_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) return result;
    result = uart_driver_install(BF_UART, 4096, 2048, 0, NULL, 0);
    if (result != ESP_OK) return result;
    if (xTaskCreate(bridge_task, "bf_bridge", 8192, NULL, 4, NULL) != pdPASS) {
        uart_driver_delete(BF_UART);
        return ESP_ERR_NO_MEM;
    }
    started = true;
    ESP_LOGI(TAG, "MSPv2 UART1 TX=%d RX=%d baud=%d; no flight-setting writes", tx, rx, CONFIG_BF_CAM_UART_BAUD);
    return ESP_OK;
}
