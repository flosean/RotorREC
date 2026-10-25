#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define RR_RSDK_HOST_TEST
#include "../main/camera/dji/dji_rs_sdk.c"

static TickType_t clock_ms;
static connect_state_t link = PROTOCOL_CONNECTED;
static int connect_result, subscribe_result;
static unsigned record_commands;
static int record_result;
static bool confirm_record, target_record, drop_link;
static int version_result;
static int verify_mode;
static camera_status_push_command_frame next_report;
/* This is the actual adapter; only IDF time/locks and the SDK transport are simulated. */
bool camera_status_initialized = true;
TickType_t xTaskGetTickCount(void) { return clock_ms; }
void vTaskDelay(TickType_t ticks) {
    clock_ms += ticks;
    if (drop_link) link = 0;
    if (confirm_record) {
        next_report.camera_status = target_record ? CAMERA_STATUS_PHOTO_OR_RECORDING : CAMERA_STATUS_LIVE_STREAMING;
        camera_status_callback(&next_report);
    }
}
int esp_read_mac(uint8_t *mac, int kind) { (void)kind; memset(mac, 0, 6); return 0; }
void data_init(void) {}
void data_register_status_update_callback(void (*callback)(void *)) { assert(callback == camera_status_callback); }
void data_register_new_status_update_callback(void (*callback)(void *)) { assert(callback == new_camera_status_callback); }
void update_camera_state_handler(void *data) { (void)data; }
void update_new_camera_state_handler(void *data) { (void)data; }
int connect_logic_protocol_connect(uint32_t id, uint8_t size, const int8_t *mac,
                                   uint32_t a, uint8_t b, uint16_t code, uint8_t c) {
    (void)id; (void)size; (void)mac; (void)a; verify_mode = b; (void)code; (void)c; return connect_result;
}
int subscript_camera_status(int mode, int frequency) { (void)mode; (void)frequency; return subscribe_result; }
connect_state_t connect_logic_get_state(void) { return link; }
bool is_camera_recording(void) { return s_snapshot.recording; }
version_query_response_frame_t *command_logic_get_version(void) {
    if (version_result < 0) return NULL;
    version_query_response_frame_t *reply = calloc(1, sizeof(*reply));
    reply->ack_result = (uint16_t)version_result;
    return reply;
}
static record_control_response_frame_t *record_reply(bool target) {
    ++record_commands; target_record = target;
    if (record_result < 0) return NULL;
    record_control_response_frame_t *reply = calloc(1, sizeof(*reply));
    reply->ret_code = (uint8_t)record_result;
    return reply;
}
record_control_response_frame_t *command_logic_start_record(void) { return record_reply(true); }
record_control_response_frame_t *command_logic_stop_record(void) { return record_reply(false); }

int main(void)
{
    dji_rs_sdk_init();
    assert(dji_rs_sdk_connect(1234, false) == DJI_RSDK_CONNECT_READY && verify_mode == 1);
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_READY && verify_mode == 0);
    version_result = 1;
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_FAILED);
    version_result = -1;
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_FAILED);
    version_result = 0;
    connect_result = CONNECT_PROTOCOL_REJECTED;
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_REJECTED);
    connect_result = -1;
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_FAILED);
    connect_result = 0; subscribe_result = -1;
    assert(dji_rs_sdk_connect(1234, true) == DJI_RSDK_CONNECT_SUBSCRIBE_FAILED);
    subscribe_result = 0;
    camera_status_push_command_frame report = {.camera_mode = CAMERA_MODE_NORMAL,
        .camera_status = CAMERA_STATUS_PHOTO_OR_RECORDING, .camera_bat_percentage = 85,
        .video_resolution = VIDEO_RESOLUTION_4K_16_9, .fps_idx = FPS_60,
        .record_time = 12, .remain_time = 90, .remain_capacity = 128000};
    camera_snapshot_t snapshot;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.valid && snapshot.recording && snapshot.record_time_valid && snapshot.remain_time_valid &&
        snapshot.remain_capacity_valid && snapshot.parameters_valid);
    assert(snapshot.record_time == 12 && snapshot.remain_time == 90 && snapshot.remain_capacity == 128000);
    assert(!strcmp(snapshot.parameters, "4K 16:9 60"));
    next_report = report; confirm_record = true;
    assert(dji_rs_sdk_set_recording(false) == 0);
    dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.recording_valid && !snapshot.recording && record_commands == 1);
    confirm_record = false;
    camera_status_callback(&report);
    assert(dji_rs_sdk_set_recording(false) == -3 && record_commands == 2); /* ACK alone is insufficient. */
    dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.recording_valid);
    assert(dji_rs_sdk_set_recording(false) == -1 && record_commands == 2);
    record_result = -1; camera_status_callback(&report);
    assert(dji_rs_sdk_set_recording(false) == -2 && record_commands == 3);
    record_result = 2; camera_status_callback(&report);
    assert(dji_rs_sdk_set_recording(false) == 2 && record_commands == 4);
    record_result = 0; camera_status_callback(&report); drop_link = true;
    assert(dji_rs_sdk_set_recording(false) == -3 && record_commands == 5);
    drop_link = false; link = PROTOCOL_CONNECTED; clock_ms = 0;
    camera_status_callback(&report);
    clock_ms = 5000; dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.valid && !snapshot.remain_time_valid && !snapshot.remain_capacity_valid);
    camera_status_callback(&report); link = 0; dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.valid && !snapshot.parameters_valid);
    link = PROTOCOL_CONNECTED;

    report.camera_mode = CAMERA_MODE_PHOTO;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.record_time_valid && !snapshot.remain_time_valid && !snapshot.parameters_valid);
    assert(snapshot.valid && !snapshot.recording_valid && dji_rs_sdk_set_recording(false) == -1);
    report.camera_mode = CAMERA_MODE_NORMAL; report.camera_status = CAMERA_STATUS_PRE_RECORDING;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.recording && !snapshot.recording_valid && dji_rs_sdk_set_recording(true) == -1);
    report.camera_status = 255;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.recording_valid);
    report.camera_status = CAMERA_STATUS_PHOTO_OR_RECORDING;
    report.camera_mode = CAMERA_MODE_SLOW_MOTION;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.record_time_valid && !snapshot.parameters_valid); // FPS field is a multiplier here.
    report.camera_mode = 255;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.record_time_valid && !snapshot.parameters_valid);
    report.camera_mode = CAMERA_MODE_NORMAL; report.fps_idx = 255;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.parameters_valid);
    report.fps_idx = FPS_240; report.video_resolution = VIDEO_RESOLUTION_2K_16_9;
    report.remain_time = report.remain_capacity = UINT32_MAX;
    camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.parameters_valid && strlen(snapshot.parameters) <= 16);
    assert(!snapshot.remain_time_valid && !snapshot.remain_capacity_valid);

    new_camera_status_push_command_frame extended = {.type_mode_name = 1, .type_mode_param = 2,
        .mode_name_length = 20, .mode_param_length = 20};
    memset(extended.mode_name, 'N', sizeof(extended.mode_name));
    memset(extended.mode_param, 'P', sizeof(extended.mode_param));
    new_camera_status_callback(&extended); dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.parameters_valid && strlen(snapshot.parameters) == 20 && snapshot.mode[20] == 0);
    extended.mode_param_length = 255;
    new_camera_status_callback(&extended); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.parameters_valid);
    report.temp_over = 3; camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.temperature_valid && snapshot.temperature == 3 && snapshot.mode_valid && snapshot.video_mode);
    report.temp_over = 255; camera_status_callback(&report); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.temperature_valid);
    dji_rs_sdk_reset(); dji_rs_sdk_get_snapshot(&snapshot);
    assert(!snapshot.valid && !snapshot.record_time_valid && !snapshot.remain_capacity_valid);
    clock_ms = UINT32_MAX - 1000;
    camera_status_callback(&report); clock_ms = 1000; dji_rs_sdk_get_snapshot(&snapshot);
    assert(snapshot.valid); // Tick wrap does not invalidate a recent report.
    puts("DJI R SDK actual callback, field units, unknown mode, stale data and invalidation tests passed.");
    return 0;
}
