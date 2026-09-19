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

static void bridge_task(void *argument)
{
    (void)argument;
    bool connected = false;
    bool map_valid = false;
    bool osd_supported = true;
    bool osd_acked[2] = {false, false};
    bool osd_attempted[2] = {false, false};
    uint32_t osd_attempt_ms[2] = {0};
    char osd_sent[2][17] = {{0}};
    bf_mode_map_t map = { .mode_bit = -1, .failsafe_bit = -1 };
    bf_record_policy_t policy = {0};
    msp_v2_reply_t reply;
    uint32_t map_ms = 0;
    unsigned failures = 0;
    bool last_input_valid = false;

    for (;;) {
        uint32_t cycle_start = now_ms();
        if (management_take_control_reset()) {
            connected = false; map_valid = false; last_input_valid = false;
            policy = (bf_record_policy_t){0};
        }
        if (management_active()) {
            connected = false; map_valid = false; last_input_valid = false;
            policy = (bf_record_policy_t){0};
            service_for(20);
            continue;
        }
        if (!connected) {
            policy = (bf_record_policy_t) {0};
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
            failures = 0;
            osd_supported = true;
            memset(osd_acked, 0, sizeof(osd_acked));
            memset(osd_attempted, 0, sizeof(osd_attempted));
            map_ms = now_ms() - 5000;
            ESP_LOGI(TAG, "Betaflight API 1.%u connected; USER%u controls camera", api_minor,
                     (unsigned)(CONFIG_BF_CAM_USER_MODE_ID - 39));
        }

        if ((uint32_t)(now_ms() - map_ms) >= 5000) {
            bf_mode_map_t next_map;
            bool next_valid = query(MSP_BOXIDS, &reply) &&
                bf_mode_map_parse(&next_map, CONFIG_BF_CAM_USER_MODE_ID, reply.payload, reply.length);
            if (!next_valid || !map_valid || next_map.mode_bit != map.mode_bit ||
                next_map.failsafe_bit != map.failsafe_bit) policy = (bf_record_policy_t) {0};
            map_valid = next_valid;
            if (map_valid) map = next_map;
            else ESP_LOGW(TAG, "USER mode / FAILSAFE map unavailable; OSD only, no fallback to ARM/AUX");
            map_ms = now_ms();
        }

        bool status_ok = query(MSP_STATUS, &reply);
        bf_mode_status_t mode = {0};
        bool input_valid = status_ok && map_valid &&
            bf_mode_status_parse(&map, reply.payload, reply.length, &mode) && !mode.failsafe;
        camera_controller_state_t camera;
        camera_controller_get_state(&camera);
        bf_record_action_t action = bf_record_policy_step(&policy, now_ms(), input_valid, mode.active, &camera);
        if (input_valid != last_input_valid) {
            ESP_LOGI(TAG, "Control input %s%s", input_valid ? "valid" : "inhibited",
                     input_valid ? "; set switch LOW before use" : "; camera recording is unchanged");
            last_input_valid = input_valid;
        }
        if (action != BF_RECORD_NONE) {
            esp_err_t result = camera_controller_request_recording(action == BF_RECORD_START);
            ESP_LOGI(TAG, "USER mode -> camera %s: %s (watch actual OSD status)",
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
            char desired[2][17];
            bf_camera_osd(&camera, desired);
            for (unsigned line = 0; line < 2; ++line) {
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
