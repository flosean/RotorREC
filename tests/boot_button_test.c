#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define RR_HOST_TEST
#include "../main/platform/boot_button.c"

static jmp_buf done;
static void (*task_entry)(void *);
static uint32_t start_tick, elapsed, duration;
static unsigned calls[3];
static bool hold_only;
static void short_action(void) { calls[0]++; }
static void long_action(void) { calls[1]++; }
static void wifi_action(void) { calls[2]++; }
int gpio_config(const gpio_config_t *c) {
    assert(c->pin_bit_mask == (1ULL << 9) && c->pull_up_en == 1 && c->mode == GPIO_MODE_INPUT); return 0;
}
int gpio_get_level(int pin) { assert(pin == 9); return elapsed < duration ? 0 : 1; }
TickType_t xTaskGetTickCount(void) { return start_tick + elapsed; }
void vTaskDelay(unsigned ticks) {
    assert(ticks == 20); elapsed += ticks;
    if (elapsed < duration) assert(calls[0] + calls[1] + calls[2] == 0);
    if ((hold_only && elapsed >= duration) || elapsed > duration + 100) longjmp(done, 1);
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)arg; (void)handle; assert(!strcmp(name, "boot_button") && stack == 3072 && priority == 4);
    task_entry = task; return 1;
}
static void gesture(uint32_t length, uint32_t start, int expected, bool held) {
    memset(calls, 0, sizeof(calls)); duration = length; start_tick = start; elapsed = 0; hold_only = held;
    if (!setjmp(done)) task_entry(NULL);
    for (int i = 0; i < 3; i++) assert(calls[i] == (unsigned)(i == expected));
}
int main(void) {
    boot_button_start(short_action, long_action, wifi_action);
    gesture(20, 0, -1, false); gesture(40, 0, -1, false);
    gesture(60, 0, 0, false); gesture(1180, 0, 0, false);
    gesture(1200, 0, 1, false); gesture(7980, 0, 1, false);
    gesture(8000, 0, 2, false); gesture(12000, 0, 2, false);
    gesture(12000, 0, -1, true); /* Holding never dispatches pairing or Wi-Fi. */
    gesture(8000, UINT32_MAX - 1000, 2, false); /* Tick wrap. */
    boot_button_start(NULL, NULL, NULL); gesture(8000, 0, -1, false);
    puts("BOOT button release, debounce, thresholds, hold and wrap tests passed"); return 0;
}
