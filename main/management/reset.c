#include "reset.h"
#include "settings.h"
#ifndef RR_HOST_TEST
#include "nvs.h"
#endif
static uint32_t s_pending;
uint32_t rr_reset_pending(void) { return s_pending; }

esp_err_t rr_reset_request(uint8_t scope)
{
    if (scope != 1 && scope != 2) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rr_reset", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "job", scope);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) s_pending = scope;
    nvs_close(handle); return err;
}
static esp_err_t erase_namespace(const char *name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err;
}
esp_err_t rr_reset_apply(uint8_t default_user)
{
    s_pending = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rr_reset", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint32_t scope = 0;
    err = nvs_get_u32(handle, "job", &scope); nvs_close(handle);
    if (err == ESP_OK) s_pending = scope;
    if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && scope == 0)) return ESP_OK;
    if (err != ESP_OK) return err;
    if (scope != 1 && scope != 2) return ESP_ERR_INVALID_STATE;
    /* Runs before any camera/BLE task starts. A durable job in its own namespace
     * survives interruption while the other namespaces are being cleared. */
    const char *profiles[] = {"camlink", "dji_rsdk", "gopro", "bt_config.conf"};
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        err = erase_namespace(profiles[i]); if (err != ESP_OK) return err;
    }
    if (scope == 2) {
        err = erase_namespace("rotorrec"); if (err != ESP_OK) return err;
        err = erase_namespace("rr_wifi"); if (err != ESP_OK) return err;
    } else {
        rr_settings_t settings = rr_settings_defaults(default_user);
        err = nvs_open("rotorrec", NVS_READWRITE, &handle);
        if (err != ESP_OK) return err;
        uint64_t combined; uint32_t legacy;
        err = nvs_get_u64(handle, "settings_v2", &combined);
        if (err == ESP_OK) {
            if (!rr_settings_decode((uint32_t)combined, &settings) ||
                !rr_control_decode((uint32_t)(combined >> 32), &settings)) err = ESP_ERR_INVALID_STATE;
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_get_u32(handle, "settings_v1", &legacy);
            if (err == ESP_OK && !rr_settings_decode(legacy, &settings)) err = ESP_ERR_INVALID_STATE;
            else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
        settings.link_paused = true; /* Do not automatically re-pair after removal. */
        combined = rr_settings_encode(&settings) | ((uint64_t)rr_control_encode(&settings) << 32);
        if (err == ESP_OK) err = nvs_set_u64(handle, "settings_v2", combined);
        if (err == ESP_OK) err = nvs_set_u64(handle, "stop_v1", 0);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle); if (err != ESP_OK) return err;
    }
    err = nvs_open("rr_reset", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "job", 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) s_pending = 0;
    nvs_close(handle); return err;
}
