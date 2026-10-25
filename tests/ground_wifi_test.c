#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#define RR_HOST_TEST
#include "../main/management/ground_wifi.c"

static bool storage_present, key_present, commit_fail, lock_available = true, held, active, answer = true;
static uint8_t once_value, staged_once, disk_key[8], staged_key[8];
static bool staged_key_present;
static unsigned failure_at, startup_calls, feed_count, leave_count, tick_count, registrations;
static int64_t now;
static void (*reply_writer)(const uint8_t *, size_t);
static void (*task_entry)(void *);
static rr_parser_t manager_parser;
static jmp_buf task_done;
static esp_err_t startup(void) { return ++startup_calls == failure_at ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "rr_wifi")); *handle = 1;
    if (!storage_present && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    staged_once = once_value; memcpy(staged_key, disk_key, 8); staged_key_present = key_present; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) {
    assert(h == 1 && !strcmp(key, "once")); *value = once_value; return storage_present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    assert(h == 1 && !strcmp(key, "once")); staged_once = value; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *size) {
    assert(h == 1 && !strcmp(key, "key") && *size == 8);
    if (!key_present) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(value, disk_key, 8); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t size) {
    assert(h == 1 && !strcmp(key, "key") && size == 8);
    memcpy(staged_key, value, 8); staged_key_present = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1); if (commit_fail) return ESP_FAIL;
    storage_present = true; once_value = staged_once; key_present = staged_key_present;
    memcpy(disk_key, staged_key, 8); return ESP_OK;
}
esp_err_t esp_read_mac(uint8_t *mac, int kind) {
    assert(kind == ESP_MAC_WIFI_SOFTAP); for (int i = 0; i < 6; i++) mac[i] = (uint8_t)i; return ESP_OK;
}
void esp_fill_random(void *bytes, size_t length) { memset(bytes, length == 8 ? 0xab : 0xcd, length); }
int64_t esp_timer_get_time(void) { return now; }
static const char *header(httpd_req_t *r, const char *name) {
    if (!strcmp(name, "Host")) return r->host;
    if (!strcmp(name, "X-RotorREC")) return r->token;
    assert(!strcmp(name, "Origin")); return r->origin;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *name, char *out, size_t size) {
    const char *value = header(r, name); if (!value || strlen(value) >= size) return ESP_FAIL;
    strcpy(out, value); return ESP_OK;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *name) {
    const char *value = header(r, name); return value ? strlen(value) : 0;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *value) {
    if (!strcmp(name, "Cache-Control")) { assert(!strcmp(value, "no-store")); r->headers |= 1; }
    else if (!strcmp(name, "X-Content-Type-Options")) { assert(!strcmp(value, "nosniff")); r->headers |= 2; }
    else { assert(!strcmp(name, "Content-Security-Policy") && strstr(value, "frame-ancestors 'none'")); r->headers |= 4; }
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) { r->type = type; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) { r->status = atoi(status); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *r, const char *data, ssize_t size) {
    assert(size >= 0 && (size_t)size <= sizeof(r->response));
    memcpy(r->response, data, (size_t)size); r->response_size = (size_t)size;
    if (!r->status) r->status = 200; return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *data) { return httpd_resp_send(r, data, (ssize_t)strlen(data)); }
esp_err_t httpd_resp_send_err(httpd_req_t *r, int code, const char *message) { r->status = code; return httpd_resp_sendstr(r, message); }
int httpd_req_recv(httpd_req_t *r, char *out, size_t size) {
    if (r->timeouts) { r->timeouts--; return HTTPD_SOCK_ERR_TIMEOUT; }
    if (!r->chunk) return 0;
    if (size > r->chunk) size = r->chunk;
    assert(r->offset + size <= r->content_len); memcpy(out, r->body + r->offset, size); r->offset += size; return (int)size;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return startup() == ESP_OK ? (void *)1 : NULL; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks) {
    assert(lock && (ticks == 50 || ticks == 5000) && !held); held = lock_available; return held;
}
void xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock && held); held = false; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)arg; (void)handle; assert(!strcmp(name, "wifi_maint") && stack == 4096 && priority == 4);
    task_entry = task; return startup() == ESP_OK ? pdPASS : 0;
}
void vTaskDelay(unsigned ticks) { assert(ticks == 20 && !held); longjmp(task_done, 1); }
esp_err_t esp_netif_init(void) { return startup(); }
esp_err_t esp_event_loop_create_default(void) { return startup(); }
void *esp_netif_create_default_wifi_ap(void) { return startup() == ESP_OK ? (void *)1 : NULL; }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; return startup(); }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage == WIFI_STORAGE_RAM); return startup(); }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_AP); return startup(); }
esp_err_t esp_wifi_set_config(int mode, const wifi_config_t *config) {
    assert(mode == WIFI_IF_AP && config->ap.authmode == WIFI_AUTH_WPA2_PSK && config->ap.max_connection == 1);
    assert(config->ap.channel == 1 && config->ap.ssid_len == strlen("RotorREC-030405"));
    assert(!strcmp((const char *)config->ap.password, "abababababababab")); return startup();
}
esp_err_t esp_wifi_start(void) { return startup(); }
bool httpd_uri_match_wildcard(const char *a, const char *b, size_t size) { (void)a; (void)b; (void)size; return true; }
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config) {
    assert(config->stack_size == 8192 && config->max_open_sockets == 4 && config->lru_purge_enable && config->recv_wait_timeout == 3);
    *server = (void *)1; return startup();
}
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route) {
    assert(server); if (route->method == HTTP_POST) assert(!strcmp(route->uri, "/api") && route->handler == api);
    else assert(!strcmp(route->uri, "/*") && route->handler == asset);
    registrations++; return startup();
}
void management_set_reply_writer(void (*writer)(const uint8_t *, size_t)) { reply_writer = writer; }
void management_feed(uint8_t byte) {
    assert(held); feed_count++;
    rr_packet_t p; if (rr_feed(&manager_parser, byte, &p) && answer) {
        uint8_t wire[RR_FRAME_MAX]; p.command |= 0x80; size_t size = rr_encode(&p, wire); reply_writer(wire, size);
    }
}
void management_tick(void) { assert(held); tick_count++; }
bool management_active(void) { return active; }
esp_err_t management_leave_wifi(void) { assert(held); leave_count++; return ESP_OK; }
static httpd_req_t request(void) {
    httpd_req_t r = {.uri = "/api", .host = "192.168.4.1", .token = s_token, .origin = "http://192.168.4.1", .chunk = 3};
    rr_packet_t p = {.command = RR_INFO, .session = 123, .sequence = 4}; r.content_len = rr_encode(&p, r.body); return r;
}
static void rejected(httpd_req_t *r, int status) {
    unsigned before = feed_count; assert(api(r) == ESP_OK && r->status == status && feed_count == before && !held);
}
static void run_tick(void) { if (!setjmp(task_done)) task_entry(NULL); }
int main(void) {
    assert(ground_wifi_init() == ESP_OK && !ground_wifi_active());
    assert(ground_wifi_start() == ESP_ERR_INVALID_STATE);
    commit_fail = true; assert(ground_wifi_request(true) != ESP_OK && !storage_present);
    char ssid[33], password[17]; assert(ground_wifi_credentials(ssid, password) != ESP_OK && !key_present);
    commit_fail = false; assert(ground_wifi_request(true) == ESP_OK && once_value == 1);
    commit_fail = true; assert(ground_wifi_init() != ESP_OK && !ground_wifi_active() && once_value == 1);
    commit_fail = false; assert(ground_wifi_init() == ESP_OK && ground_wifi_active() && once_value == 0);
    assert(ground_wifi_init() == ESP_OK && !ground_wifi_active());
    once_value = 2; assert(ground_wifi_init() == ESP_ERR_INVALID_STATE); once_value = 0;
    assert(ground_wifi_credentials(ssid, password) == ESP_OK && !strcmp(ssid, "RotorREC-030405") && !strcmp(password, "abababababababab"));
    commit_fail = true; assert(ground_wifi_credentials(ssid, password) == ESP_OK); commit_fail = false; /* Existing key needs no write. */
    assert(ground_wifi_request(true) == ESP_OK && ground_wifi_init() == ESP_OK);
    assert(ground_wifi_start() == ESP_OK && registrations == 2 && reply_writer && task_entry);
    unsigned steps = startup_calls;
    for (unsigned i = 1; i <= steps; i++) { failure_at = i; startup_calls = 0; assert(ground_wifi_start() != ESP_OK && startup_calls == i); }
    failure_at = 0; startup_calls = 0; assert(ground_wifi_start() == ESP_OK);
    httpd_req_t r = request(); r.uri = "/session"; assert(asset(&r) == ESP_OK && r.status == 200 && r.headers == 7);
    assert(strstr((char *)r.response, s_token) && !strstr((char *)r.response, password));
    const char *paths[] = {"/", "/index.html", "/style.css", "/app.mjs", "/protocol.mjs", "/serial.mjs", "/http.mjs", "/sha256.mjs"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) { r = request(); r.uri = paths[i]; assert(asset(&r) == ESP_OK && r.status == 200 && r.headers == 7 && r.response_size); }
    r = request(); r.uri = "/missing"; assert(asset(&r) == ESP_OK && r.status == 404);
    r = request(); r.host = "evil.test"; assert(asset(&r) == ESP_OK && r.status == 400);
    r = request(); r.host = "evil.test"; rejected(&r, 403);
    r = request(); r.token = NULL; rejected(&r, 403);
    r = request(); r.token = "old-token"; rejected(&r, 403);
    r = request(); r.origin = "https://evil.test"; rejected(&r, 403);
    r = request(); r.content_len = 19; rejected(&r, 400);
    r = request(); r.content_len = RR_FRAME_MAX + 1; rejected(&r, 400);
    r = request(); r.body[19] ^= 1; rejected(&r, 400);
    r = request(); r.content_len++; rejected(&r, 400); /* Trailing byte. */
    r = request(); memcpy(r.body + r.content_len, r.body, r.content_len); r.content_len *= 2; rejected(&r, 400);
    r = request(); r.chunk = 0; rejected(&r, 408);
    r = request(); r.timeouts = 3; rejected(&r, 408);
    r = request(); lock_available = false; rejected(&r, 503); lock_available = true;
    r = request(); r.timeouts = 2; now = 900; assert(api(&r) == ESP_OK && r.status == 200 && r.headers == 7);
    assert(r.response_size == r.content_len && atomic_load(&s_activity) == now && !held);
    rr_parser_t parser = {0}; rr_packet_t reply = {0}; bool got = false;
    for (size_t i = 0; i < r.response_size; i++) got |= rr_feed(&parser, r.response[i], &reply);
    assert(got && reply.command == (RR_INFO | 0x80) && reply.session == 123 && reply.sequence == 4);
    r = request(); r.host = "192.168.4.1:80"; r.origin = NULL; assert(api(&r) == ESP_OK && r.status == 200);
    r = request(); rr_packet_t maximum = {.command = RR_DATA, .length = RR_PAYLOAD_MAX};
    memset(maximum.payload, 0x5a, sizeof(maximum.payload)); r.content_len = rr_encode(&maximum, r.body);
    assert(api(&r) == ESP_OK && r.status == 200 && r.response_size == RR_FRAME_MAX);
    answer = false; r = request(); assert(api(&r) == ESP_OK && r.status == 409 && !held); answer = true;
    now += 600000001; active = true; run_tick(); assert(leave_count == 0 && tick_count == 1);
    active = false; run_tick(); assert(leave_count == 1);
    now = 1000; atomic_store(&s_activity, now); active = true; ground_wifi_exit_button(); run_tick(); assert(leave_count == 2);
    run_tick(); assert(leave_count == 2); /* Exit request consumed once. */
    puts("Wi-Fi server frame, HTTP boundary, storage, startup and idle tests passed"); return 0;
}
