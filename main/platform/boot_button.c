#include "boot_button.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_9
#define LONG_PRESS_MS 1200

typedef struct {
    boot_button_callback_t short_press;
    boot_button_callback_t long_press;
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
    bool long_handled = false;
    TickType_t pressed_at = 0;
    while (true) {
        bool now_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        if (now_pressed && !pressed) {
            pressed = true;
            long_handled = false;
            pressed_at = xTaskGetTickCount();
        } else if (now_pressed && pressed && !long_handled &&
                   pdTICKS_TO_MS(xTaskGetTickCount() - pressed_at) >= LONG_PRESS_MS) {
            long_handled = true;
            if (s_callbacks.long_press != NULL) {
                s_callbacks.long_press();
            }
        } else if (!now_pressed && pressed) {
            if (!long_handled && s_callbacks.short_press != NULL) {
                s_callbacks.short_press();
            }
            pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void boot_button_start(boot_button_callback_t short_press,
                       boot_button_callback_t long_press)
{
    s_callbacks.short_press = short_press;
    s_callbacks.long_press = long_press;
    xTaskCreate(button_task, "boot_button", 3072, NULL, 4, NULL);
}
