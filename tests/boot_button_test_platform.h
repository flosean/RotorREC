#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
#define GPIO_NUM_9 9
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define pdTICKS_TO_MS(n) ((uint32_t)(n))
#define pdMS_TO_TICKS(n) (n)
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
int gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(unsigned ticks);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
