#include <assert.h>
#include <stdio.h>
#include <string.h>
#define RR_HOST_TEST
#include "../main/management/reset.c"

/* Namespace-aware storage with commit failure and power-loss simulation. */
static const char *names[] = {"rr_reset", "camlink", "dji_rsdk", "gopro", "bt_config.conf", "rotorrec", "rr_wifi"};
static const char *keys[] = {"job", "settings_v2", "settings_v1", "stop_v1", "keep"};
static uint64_t disk[7][5], staged[7][5];
static unsigned commit_count, fail_commit_at;
static unsigned key_index(const char *key) {
    for (unsigned i = 0; i < 5; ++i) if (!strcmp(keys[i], key)) return i;
    assert(0); return 0;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    (void)mode;
    for (unsigned i = 0; i < 7; ++i) if (!strcmp(names[i], name)) {
        *handle = i; memcpy(staged[i], disk[i], sizeof(disk[i])); return ESP_OK;
    }
    assert(0); return ESP_FAIL;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_u64(nvs_handle_t h, const char *key, uint64_t *value) {
    *value = disk[h][key_index(key)]; return *value ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value) {
    uint64_t number; esp_err_t err = nvs_get_u64(h, key, &number); *value = (uint32_t)number; return err;
}
esp_err_t nvs_set_u64(nvs_handle_t h, const char *key, uint64_t value) {
    staged[h][key_index(key)] = value; return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) { return nvs_set_u64(h, key, value); }
esp_err_t nvs_erase_all(nvs_handle_t h) { memset(staged[h], 0, sizeof(staged[h])); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) {
    if (++commit_count == fail_commit_at) return ESP_FAIL;
    memcpy(disk[h], staged[h], sizeof(disk[h])); return ESP_OK;
}
static uint64_t original_settings;
static void seed(void) {
    memset(disk, 0, sizeof(disk)); s_pending = 0; commit_count = fail_commit_at = 0;
    for (unsigned i = 1; i < 7; ++i) disk[i][4] = 1234; /* profiles, layout, credentials */
    rr_settings_t settings = rr_settings_defaults(42);
    settings.record_mode = RR_RECORD_COMBI; settings.stop_delay_seconds = 30;
    original_settings = rr_settings_encode(&settings) | ((uint64_t)rr_control_encode(&settings) << 32);
    disk[5][1] = original_settings; disk[5][3] = 999;
}
static void check_result(unsigned scope) {
    assert(!disk[0][0] && !rr_reset_pending());
    for (unsigned i = 1; i <= 4; ++i) assert(!disk[i][4]);
    assert(!disk[5][3]);
    if (scope == 1) {
        rr_settings_t settings;
        assert(rr_settings_decode((uint32_t)disk[5][1], &settings) && settings.link_paused);
        settings.link_paused = false;
        assert(rr_settings_encode(&settings) == (uint32_t)original_settings);
        assert((disk[5][1] >> 32) == (original_settings >> 32));
        assert(disk[5][4] == 1234 && disk[6][4] == 1234);
    } else {
        for (unsigned i = 0; i < 5; ++i) assert(!disk[5][i] && !disk[6][i]);
    }
}
int main(void) {
    seed(); assert(rr_reset_apply(40) == ESP_OK && disk[1][4] == 1234);
    assert(rr_reset_request(0) == ESP_ERR_INVALID_ARG && rr_reset_request(3) == ESP_ERR_INVALID_ARG);
    fail_commit_at = 1; assert(rr_reset_request(2) == ESP_FAIL && !rr_reset_pending() && !disk[0][0]);
    for (unsigned scope = 1; scope <= 2; ++scope) {
        for (unsigned failure = 0; failure <= (scope == 1 ? 6U : 7U); ++failure) {
            seed(); assert(rr_reset_request((uint8_t)scope) == ESP_OK && rr_reset_pending() == scope);
            /* Restart; fail at every durable boundary, then restart again. */
            commit_count = 0; fail_commit_at = failure;
            if (failure) {
                assert(rr_reset_apply(40) == ESP_FAIL && disk[0][0] == scope);
                fail_commit_at = 0; s_pending = 0;
            }
            assert(rr_reset_apply(40) == ESP_OK); check_result(scope);
        }
    }
    seed(); disk[5][2] = (uint32_t)original_settings; disk[5][1] = 0;
    assert(rr_reset_request(1) == ESP_OK && rr_reset_apply(40) == ESP_OK);
    rr_settings_t migrated; assert(rr_settings_decode((uint32_t)disk[5][1], &migrated));
    assert(migrated.user_mode_id == 42 && migrated.link_paused);
    seed(); disk[5][1] = 42;
    assert(rr_reset_request(1) == ESP_OK && rr_reset_apply(40) == ESP_ERR_INVALID_STATE && disk[0][0] == 1);
    assert(rr_reset_request(2) == ESP_OK && rr_reset_apply(40) == ESP_OK); check_result(2);
    puts("Reset checks passed: namespace isolation, legacy migration and interruption at every commit.");
    return 0;
}
