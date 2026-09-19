#include "management.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifndef RR_HOST_TEST
#include "camera/camera_controller.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#endif

static atomic_bool s_active;
static atomic_bool s_serviced;
static bool s_reset_control;
static camera_protocol_t s_camera = CAMERA_PROTOCOL_DJI_ACTION2;
static esp_err_t s_storage;
static rr_parser_t s_parser;
static rr_packet_t s_request, s_last, s_reply;
static uint8_t s_wire[RR_FRAME_MAX];
static uint32_t s_session, s_sequence;
static int64_t s_activity, s_byte_time;
static esp_ota_handle_t s_ota;
static const esp_partition_t *s_partition;
static uint32_t s_size, s_offset;
static uint8_t s_sha[32];
static char s_version[32];
static mbedtls_sha256_context s_hash;
static bool s_updating, s_committed;

esp_err_t management_init(void)
{
    s_storage = nvs_flash_init();
    if (s_storage != ESP_OK) return s_storage;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rotorrec", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return s_storage = err;
    uint32_t setting = 0;
    err = nvs_get_u32(handle, "camera_v1", &setting);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK || setting < CAMERA_PROTOCOL_DJI_RSDK || setting > CAMERA_PROTOCOL_GOPRO)
        return s_storage = ESP_ERR_INVALID_STATE;
    s_camera = (camera_protocol_t)setting;
    return ESP_OK;
}
camera_protocol_t management_camera(void) { return s_camera; }
bool management_active(void) { return atomic_load(&s_active); }
bool management_healthy(void) { return atomic_load(&s_serviced); }
bool management_take_control_reset(void)
{
    bool reset = s_reset_control;
    s_reset_control = false;
    return reset;
}
void management_button_pair(void)
{
    if (!management_active()) camera_controller_request_pairing();
}
static void abort_update(void)
{
    if (s_updating) {
        esp_ota_abort(s_ota);
        mbedtls_sha256_free(&s_hash);
        s_updating = false;
    }
}
static bool camera_busy(void)
{
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    /* Unknown status is not permission to disrupt a connected camera. */
    return state.recording || state.saving || state.command_pending ||
        (state.phase == CAMERA_PHASE_READY && !state.recording_valid);
}
static void info(void)
{
    camera_controller_state_t state;
    camera_controller_get_state(&state);
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha[65];
    const volatile uint8_t *elf_bytes = app->app_elf_sha256; // Filled by esptool after linking.
    for (unsigned i = 0; i < 32; ++i) snprintf(elf_sha + i * 2, 3, "%02x", elf_bytes[i]);
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t status = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(esp_ota_get_running_partition(), &status);
    int n = snprintf((char *)s_reply.payload + 4, RR_PAYLOAD_MAX - 4,
        "{\"product\":\"RotorREC\",\"board\":\"%.31s\",\"version\":\"%.31s\","
        "\"protocol\":1,\"layout\":1,\"capacity\":%lu,\"camera\":%u,"
        "\"phase\":%u,\"pairing_code\":%u,\"recording\":%u,\"recording_valid\":%u,\"command_pending\":%u,"
        "\"saving\":%u,\"battery\":%u,\"battery_valid\":%u,\"storage_error\":%d,"
        "\"pending_verify\":%u,\"updating\":%u,\"offset\":%lu,\"elf_sha256\":\"%s\"}",
        app->project_name, app->version, (unsigned long)(next ? next->size : 0),
        (unsigned)s_camera, (unsigned)state.phase, state.pairing_code,
        state.recording, state.recording_valid, state.command_pending, state.saving, state.snapshot.battery,
        state.battery_valid, s_storage, status == ESP_OTA_IMG_PENDING_VERIFY,
        s_updating, (unsigned long)s_offset, elf_sha);
    if (n > 0 && n < RR_PAYLOAD_MAX - 4) s_reply.length = (uint16_t)(n + 4);
}
static esp_err_t begin_update(const rr_packet_t *p)
{
    if (p->length != 104 || s_updating || s_committed || camera_busy()) return ESP_ERR_INVALID_STATE;
    const esp_app_desc_t *app = esp_app_get_description();
    s_size = rr_u32(p->payload);
    if (rr_u32(p->payload + 4) != 1 ||
        memcmp(p->payload + 8, app->project_name, 32) != 0 ||
        !memchr(p->payload + 40, 0, 32) || p->payload[40] == 0) return ESP_ERR_INVALID_ARG;
    s_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_partition || s_partition == esp_ota_get_running_partition() ||
        s_size < 288 || s_size > s_partition->size) return ESP_ERR_INVALID_SIZE;
    memcpy(s_version, p->payload + 40, 32);
    memcpy(s_sha, p->payload + 72, 32);
    /* Incremental erase bounds each stop-and-wait transaction. */
    esp_err_t err = esp_ota_begin(s_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota);
    if (err != ESP_OK) return err;
    s_updating = true;
    s_offset = 0;
    mbedtls_sha256_init(&s_hash);
    if (mbedtls_sha256_starts(&s_hash, 0) != 0) { abort_update(); return ESP_FAIL; }
    return ESP_OK;
}
static esp_err_t update_data(const rr_packet_t *p)
{
    if (!s_updating || p->length <= 4 || p->length > 1028) return ESP_ERR_INVALID_STATE;
    size_t size = p->length - 4;
    if (rr_u32(p->payload) != s_offset || size > s_size - s_offset) return ESP_ERR_INVALID_SIZE;
    if (!s_offset) {
        /* IDF app descriptor follows the 24-byte image + 8-byte segment header. */
        if (size < 288 || p->payload[4] != 0xe9) return ESP_ERR_INVALID_ARG;
        esp_app_desc_t app;
        memcpy(&app, p->payload + 4 + 32, sizeof(app));
        if (app.magic_word != ESP_APP_DESC_MAGIC_WORD ||
            memcmp(app.project_name, esp_app_get_description()->project_name, 32) != 0 ||
            memcmp(app.version, s_version, 32) != 0) return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_ota_write(s_ota, p->payload + 4, size);
    if (err == ESP_OK && mbedtls_sha256_update(&s_hash, p->payload + 4, size) != 0) err = ESP_FAIL;
    if (err != ESP_OK) { abort_update(); return err; }
    s_offset += (uint32_t)size;
    return ESP_OK;
}
static esp_err_t end_update(void)
{
    if (!s_updating || s_offset != s_size) return ESP_ERR_INVALID_STATE;
    uint8_t actual[32];
    if (mbedtls_sha256_finish(&s_hash, actual) != 0 || memcmp(actual, s_sha, 32) != 0) {
        abort_update(); return ESP_ERR_INVALID_CRC;
    }
    mbedtls_sha256_free(&s_hash);
    esp_err_t err = esp_ota_end(s_ota); // Validates the ESP image including chip compatibility.
    s_updating = false;
    if (err != ESP_OK) return err;
    esp_app_desc_t app;
    err = esp_ota_get_partition_description(s_partition, &app);
    if (err != ESP_OK) return err;
    if (memcmp(app.project_name, esp_app_get_description()->project_name, 32) ||
        memcmp(app.version, s_version, 32)) return ESP_ERR_INVALID_ARG;
    err = esp_ota_set_boot_partition(s_partition);
    s_committed = err == ESP_OK;
    return err;
}
static esp_err_t execute(const rr_packet_t *p)
{
    if (s_updating && p->command != RR_DATA && p->command != RR_END &&
        p->command != RR_ABORT && p->command != RR_INFO) return ESP_ERR_INVALID_STATE;
    if (p->command != RR_SET_CAMERA && p->command != RR_BEGIN && p->command != RR_DATA && p->length)
        return ESP_ERR_INVALID_SIZE;
    switch (p->command) {
    case RR_HELLO: case RR_INFO: info(); return ESP_OK;
    case RR_SET_CAMERA: {
        if (s_storage != ESP_OK || s_committed || camera_busy()) return ESP_ERR_INVALID_STATE;
        if (p->length != 1 || p->payload[0] < CAMERA_PROTOCOL_DJI_RSDK ||
            p->payload[0] > CAMERA_PROTOCOL_GOPRO) return ESP_ERR_INVALID_ARG;
        nvs_handle_t handle;
        esp_err_t err = nvs_open("rotorrec", NVS_READWRITE, &handle);
        if (err != ESP_OK) return err;
        err = nvs_set_u32(handle, "camera_v1", p->payload[0]);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
        /* Keep the running selection immutable until reboot. */
        return err;
    }
    case RR_PAIR:
        if (s_committed || camera_busy()) return ESP_ERR_INVALID_STATE;
        return camera_controller_request_pairing();
    case RR_BEGIN: return begin_update(p);
    case RR_DATA: return update_data(p);
    case RR_END: return end_update();
    case RR_ABORT: abort_update(); return s_committed ? ESP_ERR_INVALID_STATE : ESP_OK;
    case RR_REBOOT: return camera_busy() ? ESP_ERR_INVALID_STATE : ESP_OK;
    case RR_EXIT: return s_committed ? ESP_ERR_INVALID_STATE : ESP_OK;
    default: return ESP_ERR_NOT_SUPPORTED;
    }
}
static void send_reply(void)
{
    size_t size = rr_encode(&s_reply, s_wire);
    uart_write_bytes(UART_NUM_1, s_wire, size);
}
void management_feed(uint8_t byte)
{
    int64_t now = esp_timer_get_time();
    if (now - s_byte_time > 300000) s_parser.used = 0;
    s_byte_time = now;
    if (!rr_feed(&s_parser, byte, &s_request) || (s_request.command & 0x80)) return;
    const rr_packet_t *p = &s_request;
    if (!p->session) return;
    if (management_active() && p->session == s_session && p->sequence == s_sequence &&
        p->command == s_last.command && p->length == s_last.length &&
        memcmp(p->payload, s_last.payload, p->length) == 0) {
        s_activity = now; send_reply(); return;
    }
    if (!management_active()) {
        if (p->command != RR_HELLO || p->sequence != 0 || p->length) return;
        s_session = p->session;
        s_sequence = 0;
        s_reset_control = true;
        atomic_store(&s_active, true);
    } else if (p->session != s_session || s_sequence == UINT32_MAX || p->sequence != s_sequence + 1) {
        return; // Do not execute stale/reordered commands or let another client steal an update.
    }
    s_sequence = p->sequence;
    s_activity = now;
    s_reply = (rr_packet_t){.command = p->command | 0x80, .session = p->session,
                            .sequence = p->sequence, .length = 4};
    esp_err_t err = execute(p);
    rr_put32(s_reply.payload, (uint32_t)err);
    s_last = *p;
    send_reply();
    if (err == ESP_OK && p->command == RR_REBOOT) {
        uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(1000));
        esp_restart();
    }
    if (err == ESP_OK && p->command == RR_EXIT) atomic_store(&s_active, false);
}
void management_tick(void)
{
    atomic_store(&s_serviced, true);
    if (management_active() && esp_timer_get_time() - s_activity > 30000000) {
        abort_update();
        // Once committed, reboot into the verified image even if the final ACK was lost.
        if (s_committed) esp_restart();
        atomic_store(&s_active, false);
    }
}
