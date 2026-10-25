#include "ground_wifi.h"
#include "management.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef RR_HOST_TEST
#include "ground_wifi_test_platform.h"
#else
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#endif

static bool s_enabled;
static SemaphoreHandle_t s_lock;
static uint8_t s_response[RR_FRAME_MAX];
static size_t s_response_size;
static char s_token[33];
static atomic_int_fast64_t s_activity;
static atomic_bool s_exit_requested;
#define TRY(call) do { esp_err_t result = (call); if (result != ESP_OK) return result; } while (0)

bool ground_wifi_active(void) { return s_enabled; }
esp_err_t ground_wifi_request(bool enabled)
{
    nvs_handle_t handle;
    TRY(nvs_open("rr_wifi", NVS_READWRITE, &handle));
    esp_err_t err = nvs_set_u8(handle, "once", enabled);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err;
}
esp_err_t ground_wifi_init(void)
{
    s_enabled = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rr_wifi", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t once = 0;
    err = nvs_get_u8(handle, "once", &once); nvs_close(handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (once > 1) return ESP_ERR_INVALID_STATE;
    if (once) { TRY(ground_wifi_request(false)); s_enabled = true; }
    return ESP_OK;
}
esp_err_t ground_wifi_credentials(char ssid[33], char password[17])
{
    uint8_t mac[6], key[8];
    TRY(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(ssid, 33, "RotorREC-%02X%02X%02X", mac[3], mac[4], mac[5]);
    nvs_handle_t handle;
    TRY(nvs_open("rr_wifi", NVS_READWRITE, &handle));
    size_t size = sizeof(key);
    esp_err_t err = nvs_get_blob(handle, "key", key, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(key, sizeof(key));
        err = nvs_set_blob(handle, "key", key, sizeof(key));
        if (err == ESP_OK) err = nvs_commit(handle);
    } else if (err == ESP_OK && size != sizeof(key)) err = ESP_ERR_INVALID_STATE;
    nvs_close(handle);
    if (err != ESP_OK) return err;
    for (unsigned i = 0; i < sizeof(key); i++) snprintf(password + i * 2, 3, "%02x", key[i]);
    return ESP_OK;
}

/* Linked from the same files tested by the desktop configurator. */
#ifdef RR_HOST_TEST
#define ASSET(name) static const uint8_t name##_start[] = #name; \
                    static const uint8_t *const name##_end = name##_start + sizeof(name##_start) - 1
#else
#define ASSET(name) extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
                    extern const uint8_t name##_end[] asm("_binary_" #name "_end")
#endif
ASSET(index_html); ASSET(style_css); ASSET(app_mjs); ASSET(protocol_mjs); ASSET(serial_mjs); ASSET(http_mjs);
ASSET(sha256_mjs);
static void headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'");
}
static bool local_host(httpd_req_t *request)
{
    char host[40];
    return httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) == ESP_OK &&
        (!strcmp(host, "192.168.4.1") || !strcmp(host, "192.168.4.1:80"));
}
static esp_err_t asset(httpd_req_t *request)
{
    if (!local_host(request)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Use http://192.168.4.1");
    headers(request); atomic_store(&s_activity, esp_timer_get_time());
    if (!strcmp(request->uri, "/session")) {
        httpd_resp_set_type(request, "application/json");
        char json[64]; snprintf(json, sizeof(json), "{\"token\":\"%s\"}", s_token);
        return httpd_resp_sendstr(request, json);
    }
    const uint8_t *start = NULL, *end = NULL; const char *type = "text/javascript";
#define SERVE(path, name, mime) if (!strcmp(request->uri, path)) { start = name##_start; end = name##_end; type = mime; }
    SERVE("/", index_html, "text/html; charset=utf-8")
    else SERVE("/index.html", index_html, "text/html; charset=utf-8")
    else SERVE("/style.css", style_css, "text/css")
    else SERVE("/app.mjs", app_mjs, "text/javascript")
    else SERVE("/protocol.mjs", protocol_mjs, "text/javascript")
    else SERVE("/serial.mjs", serial_mjs, "text/javascript")
    else SERVE("/http.mjs", http_mjs, "text/javascript")
    else SERVE("/sha256.mjs", sha256_mjs, "text/javascript")
    if (!start) return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not found");
    httpd_resp_set_type(request, type);
    return httpd_resp_send(request, (const char *)start, (ssize_t)(end - start));
}
static void capture_reply(const uint8_t *bytes, size_t size)
{
    if (size <= sizeof(s_response)) { memcpy(s_response, bytes, size); s_response_size = size; }
}
static esp_err_t api(httpd_req_t *request)
{
    char token[40], origin[48];
    if (!local_host(request) || httpd_req_get_hdr_value_str(request, "X-RotorREC", token, sizeof(token)) != ESP_OK || strcmp(token, s_token))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the device page again");
    if (httpd_req_get_hdr_value_len(request, "Origin") &&
        (httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK || strcmp(origin, "http://192.168.4.1")))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Local page required");
    if (request->content_len < 20 || request->content_len > RR_FRAME_MAX)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid frame size");
    uint8_t wire[RR_FRAME_MAX]; size_t received = 0; unsigned timeouts = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, (char *)wire + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "Incomplete frame");
        received += (size_t)count;
    }
    rr_parser_t parser = {0}; rr_packet_t packet = {0}; unsigned frames = 0;
    for (size_t i = 0; i < received; i++) if (rr_feed(&parser, wire[i], &packet)) frames++;
    if (frames != 1 || parser.used || received != (size_t)packet.length + 20)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid frame");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        httpd_resp_set_status(request, "503 Service Unavailable"); return httpd_resp_sendstr(request, "Busy");
    }
    s_response_size = 0;
    for (size_t i = 0; i < received; i++) management_feed(wire[i]);
    headers(request); atomic_store(&s_activity, esp_timer_get_time());
    httpd_resp_set_type(request, "application/octet-stream");
    if (!s_response_size) httpd_resp_set_status(request, "409 Conflict");
    esp_err_t result = s_response_size ? httpd_resp_send(request, (const char *)s_response, (ssize_t)s_response_size) :
        httpd_resp_sendstr(request, "Session unavailable; retry after 30 seconds");
    xSemaphoreGive(s_lock); return result;
}
void ground_wifi_exit_button(void) { atomic_store(&s_exit_requested, true); }
static void maintenance_task(void *unused)
{
    (void)unused;
    for (;;) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            management_tick();
            if (atomic_exchange(&s_exit_requested, false) || (!management_active() &&
                esp_timer_get_time() - atomic_load(&s_activity) > 600000000)) (void)management_leave_wifi();
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t ground_wifi_start(void)
{
    if (!s_enabled) return ESP_ERR_INVALID_STATE;
    char ssid[33], password[17]; TRY(ground_wifi_credentials(ssid, password));
    uint8_t random[16]; esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < sizeof(random); i++) snprintf(s_token + i * 2, 3, "%02x", random[i]);
    s_lock = xSemaphoreCreateMutex(); if (!s_lock) return ESP_ERR_NO_MEM;
    TRY(esp_netif_init()); TRY(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT(); TRY(esp_wifi_init(&init));
    TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config = {.ap = {.channel = 1, .max_connection = 1, .authmode = WIFI_AUTH_WPA2_PSK}};
    memcpy(config.ap.ssid, ssid, strlen(ssid)); config.ap.ssid_len = strlen(ssid);
    memcpy(config.ap.password, password, strlen(password));
    TRY(esp_wifi_set_mode(WIFI_MODE_AP)); TRY(esp_wifi_set_config(WIFI_IF_AP, &config)); TRY(esp_wifi_start());
    httpd_config_t http = HTTPD_DEFAULT_CONFIG(); http.stack_size = 8192; http.uri_match_fn = httpd_uri_match_wildcard;
    http.max_open_sockets = 4; http.lru_purge_enable = true; http.recv_wait_timeout = 3;
    httpd_handle_t server; TRY(httpd_start(&server, &http));
    const httpd_uri_t api_route = {.uri = "/api", .method = HTTP_POST, .handler = api};
    const httpd_uri_t assets = {.uri = "/*", .method = HTTP_GET, .handler = asset};
    TRY(httpd_register_uri_handler(server, &api_route)); TRY(httpd_register_uri_handler(server, &assets));
    management_set_reply_writer(capture_reply); atomic_store(&s_activity, esp_timer_get_time());
    if (xTaskCreate(maintenance_task, "wifi_maint", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI("WIFI_SETUP", "Ground setup: SSID=%s password=%s URL=http://192.168.4.1 (camera/FC control disabled)", ssid, password);
    return ESP_OK;
}
