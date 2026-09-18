#include "status_led.h"
#include "status_led_color.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static rmt_symbol_word_t s_symbols[25];
static uint32_t s_last_color = UINT32_MAX;

esp_err_t status_led_init(void)
{
    rmt_tx_channel_config_t config = {
        .gpio_num = 10, // Waveshare C3-Zero WS2812, not a generic GPIO LED.
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
    };
    esp_err_t result = rmt_new_tx_channel(&config, &s_channel);
    if (result != ESP_OK) return result;
    rmt_copy_encoder_config_t encoder_config = {};
    result = rmt_new_copy_encoder(&encoder_config, &s_encoder);
    if (result != ESP_OK) {
        rmt_del_channel(s_channel);
        s_channel = NULL;
        return result;
    }
    result = rmt_enable(s_channel);
    if (result != ESP_OK) {
        rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_channel = NULL;
        return result;
    }
    status_led_set_rgb(12, 0, 0);
    if (!s_channel) return ESP_FAIL;
    ESP_LOGI("STATUS_LED", "WS2812 GPIO10 ready (RGB order, hardware RMT, low brightness)");
    return ESP_OK;
}

void status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t color = status_led_wire_color(red, green, blue);
    if (!s_channel || color == s_last_color) return;
    /* All 24 RGB bits plus reset fit in C3's hardware RMT memory. No bit-banging
       or long critical section that could starve BLE during LED updates. */
    for (unsigned i = 0; i < 24; ++i) {
        bool bit = (color & (1UL << (23 - i))) != 0;
        s_symbols[i] = (rmt_symbol_word_t) {
            .level0 = 1, .duration0 = bit ? 9 : 3,
            .level1 = 0, .duration1 = bit ? 3 : 9,
        };
    }
    s_symbols[24] = (rmt_symbol_word_t) { .level0 = 0, .duration0 = 1500,
                                       .level1 = 0, .duration1 = 1500 };
    rmt_transmit_config_t tx = { .loop_count = 0 };
    esp_err_t result = rmt_transmit(s_channel, s_encoder, s_symbols, sizeof(s_symbols), &tx);
    if (result == ESP_OK) result = rmt_tx_wait_all_done(s_channel, 20);
    if (result == ESP_OK) {
        s_last_color = color;
    } else {
        ESP_LOGE("STATUS_LED", "RMT failed: %s; LED disabled, camera continues", esp_err_to_name(result));
        /* Never modify an in-flight buffer after a timeout. */
        rmt_disable(s_channel);
        s_channel = NULL;
    }
}
