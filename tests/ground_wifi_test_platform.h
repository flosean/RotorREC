#pragma once
#include "management_test_platform.h"
typedef ptrdiff_t ssize_t;
typedef void *SemaphoreHandle_t;
typedef void *httpd_handle_t;
#define ESP_ERR_NO_MEM 0x101
#define ESP_MAC_WIFI_SOFTAP 1
#define pdTRUE 1
#define pdPASS 1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_408_REQ_TIMEOUT 408
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTP_GET 0
#define HTTP_POST 1
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_AP 2
#define WIFI_IF_AP 1
#define WIFI_AUTH_WPA2_PSK 3
#define ESP_LOGI(...) ((void)0)
typedef struct {
    const char *uri, *host, *token, *origin;
    size_t content_len, offset, chunk;
    uint8_t body[RR_FRAME_MAX * 2], response[RR_FRAME_MAX];
    size_t response_size;
    unsigned timeouts, headers;
    int status;
    const char *type;
} httpd_req_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct { struct { uint8_t ssid[32], password[64]; size_t ssid_len;
    int channel, max_connection, authmode; } ap; } wifi_config_t;
typedef struct { unsigned stack_size, max_open_sockets, recv_wait_timeout;
    bool lru_purge_enable; bool (*uri_match_fn)(const char *, const char *, size_t); } httpd_config_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value);
esp_err_t esp_read_mac(uint8_t *mac, int kind);
void esp_fill_random(void *bytes, size_t length);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *name, char *out, size_t size);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *name);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *value);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *data, ssize_t size);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *data);
esp_err_t httpd_resp_send_err(httpd_req_t *r, int code, const char *message);
int httpd_req_recv(httpd_req_t *r, char *out, size_t size);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks);
void xSemaphoreGive(SemaphoreHandle_t lock);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
void vTaskDelay(unsigned ticks);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
void *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int mode, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
bool httpd_uri_match_wildcard(const char *a, const char *b, size_t size);
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
