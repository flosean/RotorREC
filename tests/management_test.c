#include <assert.h>
#include <stdio.h>
#include <string.h>
#define RR_HOST_TEST
#include "../main/management/management.c"

static int64_t clock_us;
static uint8_t flash[4096], response[RR_FRAME_MAX];
static size_t written, response_size;
static unsigned writes, switches, restarts, aborts, pairs, commits;
static esp_err_t fail_write, fail_end, fail_switch, fail_commit;
static esp_err_t fail_set, fail_open, fail_get;
static uint32_t saved_camera, saved_settings, staged_camera, staged_settings;
static camera_controller_state_t camera;
static esp_partition_t running = {4096}, spare = {4096};
static esp_app_desc_t app = {.magic_word = ESP_APP_DESC_MAGIC_WORD,
                             .project_name = "rotorrec_esp32c3", .version = "1.1.0"};

void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
int mbedtls_sha256_starts(mbedtls_sha256_context *c, int unused) {
    (void)unused;
    if (BCryptOpenAlgorithmProvider(&c->algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)) return -1;
    return BCryptCreateHash(c->algorithm, &c->hash, NULL, 0, NULL, 0, 0) ? -1 : 0;
}
int mbedtls_sha256_update(mbedtls_sha256_context *c, const uint8_t *data, size_t size) {
    return BCryptHashData(c->hash, (PUCHAR)data, (ULONG)size, 0) ? -1 : 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context *c, uint8_t *out) {
    return BCryptFinishHash(c->hash, out, 32, 0) ? -1 : 0;
}
void mbedtls_sha256_free(mbedtls_sha256_context *c) {
    if (c->hash) BCryptDestroyHash(c->hash);
    if (c->algorithm) BCryptCloseAlgorithmProvider(c->algorithm, 0);
    memset(c, 0, sizeof(*c));
}
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(strcmp(name, "rotorrec") == 0);
    if (fail_open) return fail_open;
    if (mode == NVS_READWRITE) { staged_camera = saved_camera; staged_settings = saved_settings; }
    *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *name, uint32_t *value) {
    (void)h;
    if (fail_get) return fail_get;
    assert(!strcmp(name, "camera_v1") || !strcmp(name, "settings_v1"));
    *value = !strcmp(name, "camera_v1") ? saved_camera : saved_settings;
    return *value ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char *name, uint32_t value) {
    (void)h;
    if (fail_set) return fail_set;
    if (!strcmp(name, "camera_v1")) staged_camera = value;
    else { assert(!strcmp(name, "settings_v1")); staged_settings = value; }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h; ++commits;
    if (!fail_commit) { saved_camera = staged_camera; saved_settings = staged_settings; }
    return fail_commit;
}
void nvs_close(nvs_handle_t h) { (void)h; }
const esp_app_desc_t *esp_app_get_description(void) { return &app; }
const esp_partition_t *esp_ota_get_next_update_partition(const void *unused) { (void)unused; return &spare; }
const esp_partition_t *esp_ota_get_running_partition(void) { return &running; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *state) {
    (void)p; *state = ESP_OTA_IMG_UNDEFINED; return ESP_OK;
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *h) {
    assert(p == &spare && size == OTA_WITH_SEQUENTIAL_WRITES); *h = 1; written = 0; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t size) {
    (void)h; ++writes;
    if (fail_write) return fail_write;
    assert(written + size <= sizeof(flash)); memcpy(flash + written, data, size); written += size; return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; return fail_end; }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; ++aborts; return ESP_OK; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *a) {
    assert(p == &spare); memcpy(a, flash + 32, sizeof(*a)); return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) {
    assert(p == &spare); if (!fail_switch) ++switches; return fail_switch;
}
int64_t esp_timer_get_time(void) { return clock_us; }
int uart_write_bytes(int uart, const void *data, size_t size) {
    assert(uart == 1 && size <= sizeof(response)); memcpy(response, data, size); response_size = size; return (int)size;
}
int uart_wait_tx_done(int uart, int ticks) { (void)uart; (void)ticks; return 0; }
void esp_restart(void) { ++restarts; }
void camera_controller_get_state(camera_controller_state_t *state) { *state = camera; }
esp_err_t camera_controller_request_pairing(void) { ++pairs; return ESP_OK; }

static rr_packet_t request;
static esp_err_t transact(void)
{
    uint8_t bytes[RR_FRAME_MAX];
    size_t size = rr_encode(&request, bytes);
    response_size = 0;
    for (size_t i = 0; i < size; ++i) management_feed(bytes[i]);
    if (!response_size) return -99;
    rr_parser_t parser = {0}; rr_packet_t reply = {0};
    bool found = false;
    for (size_t i = 0; i < response_size; ++i) found |= rr_feed(&parser, response[i], &reply);
    assert(found && reply.sequence == request.sequence && reply.command == (request.command | 0x80));
    return (esp_err_t)rr_u32(reply.payload);
}
static void command(uint8_t cmd) { ++request.sequence; request.command = cmd; request.length = 0; }
static void reset(void)
{
    abort_update(); s_committed = false; atomic_store(&s_active, false);
    memset(&s_parser, 0, sizeof(s_parser)); memset(&s_last, 0, sizeof(s_last));
    s_camera = CAMERA_PROTOCOL_DJI_ACTION2; s_offset = 0;
    camera = (camera_controller_state_t){0};
    writes = switches = restarts = aborts = pairs = commits = 0;
    fail_write = fail_end = fail_switch = fail_commit = 0;
    fail_set = fail_open = fail_get = 0;
    atomic_store(&s_resume_requested, false);
    assert(management_init() == ESP_OK);
    request = (rr_packet_t){.command = RR_HELLO, .session = 123};
    assert(transact() == ESP_OK && management_active());
}
static uint8_t image[1536];
static void begin(void)
{
    memset(image, 0xa5, sizeof(image)); image[0] = 0xe9;
    memcpy(image + 32, &app, sizeof(app));
    command(RR_BEGIN); request.length = 104;
    memset(request.payload, 0, request.length);
    rr_put32(request.payload, sizeof(image)); rr_put32(request.payload + 4, 1);
    memcpy(request.payload + 8, app.project_name, 32);
    memcpy(request.payload + 40, app.version, 32);
    mbedtls_sha256_context hash; mbedtls_sha256_init(&hash);
    assert(!mbedtls_sha256_starts(&hash, 0));
    assert(!mbedtls_sha256_update(&hash, image, sizeof(image)));
    assert(!mbedtls_sha256_finish(&hash, request.payload + 72));
    mbedtls_sha256_free(&hash);
}
static void chunk(unsigned offset, unsigned size)
{
    command(RR_DATA); request.length = (uint16_t)(size + 4);
    rr_put32(request.payload, offset); memcpy(request.payload + 4, image + offset, size);
}
static void transfer(void)
{
    begin(); assert(transact() == ESP_OK);
    chunk(0, 1024); assert(transact() == ESP_OK);
    assert(transact() == ESP_OK && writes == 1); // Lost ACK replay must not rewrite flash.
    chunk(1024, 512); assert(transact() == ESP_OK);
}

static void settings_tests(void)
{
    saved_camera = saved_settings = 0;
    reset();
    assert(rr_settings_encode(management_settings()) == 0x01000050U);
    assert(strstr((char *)s_reply.payload + 4, "\"settings_raw\":16777296"));
    rr_settings_t settings = rr_settings_defaults(43);
    settings.osd_lines = 4; settings.owns_extra_lines = true;
    settings.led_percent = 25; settings.low_battery_percent = 20; settings.link_paused = true;
    uint32_t expected = rr_settings_encode(&settings);
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, expected);
    assert(transact() == ESP_OK && saved_settings == expected && commits == 1);
    assert(transact() == ESP_OK && commits == 1); // ACK loss must not write NVS twice.
    assert(!management_settings()->link_paused); // Immutable until restart.
    command(RR_PAIR); assert(transact() == ESP_ERR_INVALID_STATE && pairs == 0);
    command(RR_INFO); assert(transact() == ESP_OK && s_reply.length > 4);
    assert(rr_settings_encode(&s_saved_settings) == expected);
    assert(management_init() == ESP_OK && management_settings()->link_paused);
    command(RR_PAIR); assert(transact() == ESP_ERR_INVALID_STATE);
    command(RR_EXIT); assert(transact() == ESP_OK);
    fail_commit = ESP_FAIL;
    management_button_pair(); management_tick();
    assert(restarts == 0 && saved_settings == expected);
    fail_commit = ESP_OK;
    management_button_pair(); management_tick();
    assert(restarts == 1 && pairs == 0);
    settings.link_paused = false;
    assert(saved_settings == rr_settings_encode(&settings));
    assert(management_init() == ESP_OK && !management_settings()->link_paused);

    reset();
    /* A two-line save cannot abandon ownership and leave old lines behind. */
    settings.osd_lines = 2; settings.owns_extra_lines = false;
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, rr_settings_encode(&settings));
    assert(transact() == ESP_OK && s_saved_settings.owns_extra_lines);
    uint32_t previous = saved_settings;
    command(RR_SET_SETTINGS); request.length = 3;
    assert(transact() == ESP_ERR_INVALID_SIZE && saved_settings == previous);
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous | (1U << 19));
    assert(transact() == ESP_ERR_INVALID_ARG && saved_settings == previous);
    const camera_controller_state_t blocked[] = {
        {.recording = true, .link_connected = true}, {.saving = true, .link_connected = true},
        {.command_pending = true},
        {.phase = CAMERA_PHASE_READY},
        {.phase = CAMERA_PHASE_VERIFYING, .link_connected = true},
        {.phase = CAMERA_PHASE_WAITING_STATUS, .link_connected = true},
    };
    for (unsigned i = 0; i < sizeof(blocked) / sizeof(blocked[0]); ++i) {
        camera = blocked[i];
        command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous);
        assert(transact() == ESP_ERR_INVALID_STATE && saved_settings == previous);
        command(RR_REBOOT); assert(transact() == ESP_ERR_INVALID_STATE);
    }
    camera = (camera_controller_state_t){0}; // Offline is available for recovery.
    fail_open = ESP_FAIL;
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous);
    assert(transact() == ESP_FAIL && saved_settings == previous);
    fail_open = 0; fail_set = ESP_FAIL;
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous);
    assert(transact() == ESP_FAIL && saved_settings == previous);
    fail_set = 0; fail_commit = ESP_FAIL;
    settings.low_battery_percent = 50;
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, rr_settings_encode(&settings));
    assert(transact() == ESP_FAIL && saved_settings == previous);
    assert(rr_settings_encode(&s_saved_settings) == previous);
    fail_commit = 0;
    /* Last-known recording/saving after BLE loss must not block offline pause/recovery. */
    camera = (camera_controller_state_t){.phase = CAMERA_PHASE_RECONNECTING,
        .recording = true, .saving = true, .recording_valid = false, .link_connected = false};
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous);
    assert(transact() == ESP_OK);
    command(RR_REBOOT); assert(transact() == ESP_OK);
    begin(); assert(transact() == ESP_OK);
    command(RR_SET_SETTINGS); request.length = 4; rr_put32(request.payload, previous);
    assert(transact() == ESP_ERR_INVALID_STATE);
    command(RR_ABORT); assert(transact() == ESP_OK);

    saved_settings = 0x02000050U;
    assert(management_init() == ESP_ERR_INVALID_STATE);
    saved_settings = 0; fail_get = ESP_FAIL;
    assert(management_init() == ESP_FAIL);
    fail_get = 0;
    assert(management_init() == ESP_OK);
}
int main(void)
{
    _Static_assert(sizeof(esp_app_desc_t) == 256, "app descriptor layout");
    assert(rr_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926);
    rr_packet_t packet = {.command = RR_DATA, .session = 77, .sequence = 42, .length = RR_PAYLOAD_MAX};
    for (size_t i = 0; i < sizeof(packet.payload); ++i) packet.payload[i] = (uint8_t)i;
    uint8_t wire[RR_FRAME_MAX]; size_t length = rr_encode(&packet, wire);
    rr_parser_t parser = {0}; rr_packet_t decoded = {0};
    for (size_t i = 0; i < length; ++i) assert(rr_feed(&parser, wire[i], &decoded) == (i == length - 1));
    assert(decoded.session == 77 && decoded.length == RR_PAYLOAD_MAX &&
           !memcmp(decoded.payload, packet.payload, RR_PAYLOAD_MAX));
    wire[length - 1] ^= 1;
    for (size_t i = 0; i < length; ++i) assert(!rr_feed(&parser, wire[i], &decoded));
    uint32_t random = 1;
    for (unsigned i = 0; i < 1000000; ++i) {
        random = random * 1664525U + 1013904223U;
        rr_feed(&parser, (uint8_t)(random >> 24), &decoded);
        assert(parser.used < RR_FRAME_MAX);
    }
    memset(&parser, 0, sizeof(parser)); wire[length - 1] ^= 1;
    for (size_t i = 0; i < length; ++i) assert(rr_feed(&parser, wire[i], &decoded) == (i == length - 1));
    reset(); assert(transact() == ESP_OK); // HELLO replay.
    command(RR_EXIT); assert(transact() == ESP_OK && !management_active());
    assert(management_take_control_reset()); // Even a session shorter than one MSP loop resets USER policy.
    assert(!management_take_control_reset());
    reset();
    command(RR_PAIR); assert(transact() == ESP_OK && pairs == 1);
    assert(transact() == ESP_OK && pairs == 1);
    request.session++; assert(transact() == -99); request.session--;
    command(RR_SET_CAMERA); request.length = 1; request.payload[0] = 3;
    assert(transact() == ESP_OK && saved_camera == 3 && commits == 1);
    assert(transact() == ESP_OK && commits == 1);
    assert(management_camera() == CAMERA_PROTOCOL_DJI_ACTION2); // Takes effect only on reboot.
    assert(management_init() == ESP_OK && management_camera() == CAMERA_PROTOCOL_GOPRO);
    fail_commit = ESP_FAIL; command(RR_SET_CAMERA); request.length = 1; request.payload[0] = 2;
    assert(transact() == ESP_FAIL && saved_camera == 3);
    reset(); camera.recording = true; camera.link_connected = true;
    begin(); assert(transact() != ESP_OK && !s_updating);
    reset(); camera.command_pending = true; begin(); assert(transact() != ESP_OK);
    reset(); begin(); request.payload[8] ^= 1; assert(transact() != ESP_OK);
    reset(); begin(); rr_put32(request.payload, 4097); assert(transact() != ESP_OK);
    reset(); begin(); assert(transact() == ESP_OK);
    chunk(1, 1024); assert(transact() != ESP_OK && writes == 0);
    chunk(0, 1024); request.payload[4 + 80] ^= 1; assert(transact() != ESP_OK && writes == 0);
    command(RR_END); assert(transact() != ESP_OK && switches == 0);
    command(RR_ABORT); assert(transact() == ESP_OK && aborts == 1);
    reset(); transfer(); command(RR_END); assert(transact() == ESP_OK && switches == 1);
    assert(transact() == ESP_OK && switches == 1); // END replay.
    command(RR_ABORT); assert(transact() != ESP_OK && switches == 1);
    command(RR_REBOOT); assert(transact() == ESP_OK && restarts == 1);
    reset(); begin(); request.payload[72] ^= 1; assert(transact() == ESP_OK);
    chunk(0, 1024); assert(transact() == ESP_OK); chunk(1024, 512); assert(transact() == ESP_OK);
    command(RR_END); assert(transact() == ESP_ERR_INVALID_CRC && switches == 0 && aborts == 1);
    reset(); begin(); assert(transact() == ESP_OK); fail_write = ESP_FAIL;
    chunk(0, 1024); assert(transact() == ESP_FAIL && !s_updating && switches == 0);
    reset(); transfer(); fail_end = ESP_FAIL; command(RR_END); assert(transact() == ESP_FAIL && switches == 0);
    reset(); transfer(); fail_switch = ESP_FAIL; command(RR_END); assert(transact() == ESP_FAIL && switches == 0);
    reset(); begin(); assert(transact() == ESP_OK); clock_us += 30000001; management_tick();
    assert(!management_active() && !s_updating && aborts == 1 && switches == 0);
    reset(); transfer(); command(RR_END); assert(transact() == ESP_OK);
    clock_us += 30000001; management_tick(); assert(restarts == 1);
    settings_tests();
    puts("Management fault-injection checks passed (real protocol/state machine; simulated IDF storage/OTA).");
    return 0;
}
