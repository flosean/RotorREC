#pragma once
/* Native fault-injection boundary for the real management.c implementation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include <bcrypt.h>
#include "camera/camera_types.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
#define UART_NUM_1 1
#define pdMS_TO_TICKS(n) (n)
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef unsigned esp_ota_handle_t;
typedef struct { uint32_t size; } esp_partition_t;
typedef enum { ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_PENDING_VERIFY } esp_ota_img_states_t;
typedef struct {
    uint32_t magic_word, secure_version, reserved[2];
    char version[32], project_name[32], time[16], date[16], idf_ver[32];
    uint8_t app_elf_sha256[32], rest[80];
} esp_app_desc_t;
typedef struct { BCRYPT_ALG_HANDLE algorithm; BCRYPT_HASH_HANDLE hash; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *c);
int mbedtls_sha256_starts(mbedtls_sha256_context *c, int unused);
int mbedtls_sha256_update(mbedtls_sha256_context *c, const uint8_t *data, size_t size);
int mbedtls_sha256_finish(mbedtls_sha256_context *c, uint8_t *out);
void mbedtls_sha256_free(mbedtls_sha256_context *c);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *name, uint32_t *value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *name, uint32_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
const esp_app_desc_t *esp_app_get_description(void);
const esp_partition_t *esp_ota_get_next_update_partition(const void *unused);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *state);
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t h);
esp_err_t esp_ota_abort(esp_ota_handle_t h);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *app);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p);
int64_t esp_timer_get_time(void);
int uart_write_bytes(int uart, const void *data, size_t size);
int uart_wait_tx_done(int uart, int ticks);
void esp_restart(void);
void camera_controller_get_state(camera_controller_state_t *state);
esp_err_t camera_controller_request_pairing(void);
