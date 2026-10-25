#include "boot_button.h"

#include <stdbool.h>

#ifdef RR_HOST_TEST
#include "boot_button_test_platform.h"
#else
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define BOOT_BUTTON_GPIO GPIO_NUM_9
#define LONG_PRESS_MS 1200

typedef struct {
    boot_button_callback_t short_press;
    boot_button_callback_t long_press;
    boot_button_callback_t wifi_press;
} callbacks_t;

static callbacks_t s_callbacks;

static void button_task(void *argument)
{
    (void)argument;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);

    bool pressed = false;
    TickType_t pressed_at = 0;
    while (true) {
        bool now_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        if (now_pressed && !pressed) {
            pressed = true;
            pressed_at = xTaskGetTickCount();
        } else if (!now_pressed && pressed) {
            /* Dispatch on release, so the eight-second Wi-Fi gesture cannot
             * first trigger pairing. Ignore contact bounce shorter than 60 ms. */
            uint32_t held = pdTICKS_TO_MS(xTaskGetTickCount() - pressed_at);
            boot_button_callback_t action = held >= 8000 ? s_callbacks.wifi_press :
                held >= LONG_PRESS_MS ? s_callbacks.long_press : held >= 60 ? s_callbacks.short_press : NULL;
            if (action) action();
            pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void boot_button_start(boot_button_callback_t short_press,
                       boot_button_callback_t long_press, boot_button_callback_t wifi_press)
{
    s_callbacks.short_press = short_press;
    s_callbacks.long_press = long_press;
    s_callbacks.wifi_press = wifi_press;
    xTaskCreate(button_task, "boot_button", 3072, NULL, 4, NULL);
}
