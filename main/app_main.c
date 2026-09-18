#include <assert.h>
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "betaflight/osd_messages.h"
#include "betaflight/bf_bridge.h"
#include "camera/camera_controller.h"
#include "platform/boot_button.h"
#ifdef CONFIG_BF_CAM_LCD
#include "platform/display.h"
#include "ui/dashboard.h"
#else
#include "ui/headless_status.h"
#include "ui/status_indicator.h"
#include "platform/status_led.h"
#include "esp_timer.h"
#endif

static const char *TAG = "RotorREC";

static void run_self_check(void)
{
    assert(camera_controller_reconnect_delay_ms(0) == 1000);
    assert(camera_controller_reconnect_delay_ms(1) == 1000);
    assert(camera_controller_reconnect_delay_ms(3) == 1000);
    assert(camera_controller_reconnect_delay_ms(20) == 1000);
    assert(betaflight_protocol_self_check());
}

void app_main(void)
{
    ESP_LOGI(TAG, "RotorREC %s", esp_app_get_description()->version);
    run_self_check();
#ifdef CONFIG_BF_CAM_LCD
    display_init();
    dashboard_init();
#else
    ESP_LOGI(TAG, "ESP32-C3-Zero: headless camera / Betaflight bridge; BOOT=GPIO9");
    esp_err_t led_result = status_led_init();
    if (led_result != ESP_OK) ESP_LOGE(TAG, "Status LED initialization failed: %s", esp_err_to_name(led_result));
#endif

    if (camera_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Camera controller initialization failed");
    } else {
        camera_controller_start();
    }
    boot_button_start(camera_controller_short_press, camera_controller_request_pairing);
    esp_err_t bf_result = betaflight_bridge_start();
    if (bf_result != ESP_OK) ESP_LOGE(TAG, "Betaflight UART failed: %s", esp_err_to_name(bf_result));

    while (true) {
        camera_controller_state_t state;
        camera_controller_get_state(&state);
#ifdef CONFIG_BF_CAM_LCD
        dashboard_refresh(&state);
        display_process();
#else
        headless_status_refresh(&state);
        indicator_rgb_t pixel = status_indicator_pixel(status_indicator_select(&state),
                                                        (uint32_t)(esp_timer_get_time() / 1000));
        status_led_set_rgb(pixel.red, pixel.green, pixel.blue);
#endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
