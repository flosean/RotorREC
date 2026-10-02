#include "dji_rs_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RR_RSDK_HOST_TEST
#include "dji_rs_sdk_test_platform.h"
#else
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "command_logic.h"
#include "connect_logic.h"
#include "data.h"
#include "dji_protocol_data_structures.h"
#include "enums_logic.h"
#include "status_logic.h"
#endif

static portMUX_TYPE s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static camera_snapshot_t s_snapshot;
static TickType_t s_snapshot_tick;
static TickType_t s_parameters_tick;

static const char *resolution_name(uint8_t resolution)
{
    switch (resolution) {
        case VIDEO_RESOLUTION_1080P: return "1080P";
        case VIDEO_RESOLUTION_4K_16_9: return "4K 16:9";
        case VIDEO_RESOLUTION_2K_16_9: return "2.7K 16:9";
        case VIDEO_RESOLUTION_1080P_9_16: return "1080P 9:16";
        case VIDEO_RESOLUTION_2K_9_16: return "2.7K 9:16";
        case VIDEO_RESOLUTION_2K_4_3: return "2.7K 4:3";
        case VIDEO_RESOLUTION_4K_4_3: return "4K 4:3";
        case VIDEO_RESOLUTION_4K_9_16: return "4K 9:16";
        default: return "--";
    }
}

static const char *fps_name(uint8_t fps)
{
    switch (fps) {
        case FPS_24: return "24";
        case FPS_25: return "25";
        case FPS_30: return "30";
        case FPS_48: return "48";
        case FPS_50: return "50";
        case FPS_60: return "60";
        case FPS_100: return "100";
        case FPS_120: return "120";
        case FPS_200: return "200";
        case FPS_240: return "240";
        default: return "--";
    }
}

static const char *mode_name(uint8_t mode)
{
    switch (mode) {
        case CAMERA_MODE_NORMAL: return "VIDEO";
        case CAMERA_MODE_SLOW_MOTION: return "SLOW MOTION";
        case CAMERA_MODE_TIMELAPSE: return "TIMELAPSE";
        case CAMERA_MODE_PHOTO: return "PHOTO";
        case CAMERA_MODE_HYPERLAPSE: return "HYPERLAPSE";
        case CAMERA_MODE_SUPERNIGHT: return "SUPERNIGHT";
        case CAMERA_MODE_PANORAMIC_VIDEO_360: return "360 VIDEO";
        default: return "CAMERA";
    }
}

static void camera_status_callback(void *data)
{
    const camera_status_push_command_frame *status = data;
    if (status != NULL) {
        portENTER_CRITICAL(&s_snapshot_mux);
        s_snapshot.valid = true;
        s_snapshot_tick = xTaskGetTickCount();
        s_snapshot.recording = status->camera_status == CAMERA_STATUS_PHOTO_OR_RECORDING ||
                               status->camera_status == CAMERA_STATUS_PRE_RECORDING;
        s_snapshot.battery = status->camera_bat_percentage;
        s_snapshot.record_time = status->record_time;
        s_snapshot.remain_time = status->remain_time;
        s_snapshot.remain_capacity = status->remain_capacity;
        /* Photo/burst fields use different units; unknown modes remain unknown. */
        bool video = status->camera_mode == CAMERA_MODE_NORMAL ||
                     status->camera_mode == CAMERA_MODE_SLOW_MOTION ||
                     status->camera_mode == CAMERA_MODE_TIMELAPSE ||
                     status->camera_mode == CAMERA_MODE_HYPERLAPSE ||
                     status->camera_mode == CAMERA_MODE_SUPERNIGHT;
        s_snapshot.record_time_valid = video;
        s_snapshot.remain_time_valid = video && status->remain_time != UINT32_MAX;
        s_snapshot.remain_capacity_valid = status->remain_capacity != UINT32_MAX;
        /* Only normal video FPS has the meanings represented by this formatter. */
        s_snapshot.parameters_valid = (status->camera_mode == CAMERA_MODE_NORMAL ||
            status->camera_mode == CAMERA_MODE_SUPERNIGHT) &&
            strcmp(resolution_name(status->video_resolution), "--") != 0 &&
            strcmp(fps_name(status->fps_idx), "--") != 0;
        s_parameters_tick = s_snapshot_tick;
        snprintf(s_snapshot.mode, sizeof(s_snapshot.mode), "%s", mode_name(status->camera_mode));
        snprintf(s_snapshot.parameters, sizeof(s_snapshot.parameters), "%s %s",
                 resolution_name(status->video_resolution), fps_name(status->fps_idx));
        portEXIT_CRITICAL(&s_snapshot_mux);
    }
    update_camera_state_handler(data);
}

static void new_camera_status_callback(void *data)
{
    const new_camera_status_push_command_frame *status = data;
    if (status != NULL) {
        size_t name_length = status->mode_name_length > 20 ? 20 : status->mode_name_length;
        size_t parameter_length = status->mode_param_length > 20 ? 20 : status->mode_param_length;
        portENTER_CRITICAL(&s_snapshot_mux);
        memcpy(s_snapshot.mode, status->mode_name, name_length);
        s_snapshot.mode[name_length] = '\0';
        memcpy(s_snapshot.parameters, status->mode_param, parameter_length);
        s_snapshot.parameters[parameter_length] = '\0';
        s_snapshot.parameters_valid = status->type_mode_name == 1 && status->type_mode_param == 2 &&
            status->mode_param_length > 0 && status->mode_param_length <= 20;
        s_parameters_tick = xTaskGetTickCount();
        portEXIT_CRITICAL(&s_snapshot_mux);
    }
    update_new_camera_state_handler(data);
}

void dji_rs_sdk_init(void)
{
    data_init();
    data_register_status_update_callback(camera_status_callback);
    data_register_new_status_update_callback(new_camera_status_callback);
}

void dji_rs_sdk_reset(void)
{
    portENTER_CRITICAL(&s_snapshot_mux);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    portEXIT_CRITICAL(&s_snapshot_mux);
}

dji_rs_sdk_connect_result_t dji_rs_sdk_connect(uint16_t pairing_code)
{
    uint8_t bluetooth_mac[6];
    int8_t controller_mac[6];

    ESP_ERROR_CHECK(esp_read_mac(bluetooth_mac, ESP_MAC_BT));
    memcpy(controller_mac, bluetooth_mac, sizeof(controller_mac));
    if (connect_logic_protocol_connect(0x12345678, sizeof(controller_mac), controller_mac,
                                       0, 1, pairing_code, 0) != 0) {
        return DJI_RSDK_CONNECT_FAILED;
    }

    version_query_response_frame_t *version = command_logic_get_version();
    free(version);
    if (subscript_camera_status(PUSH_MODE_PERIODIC_WITH_STATE_CHANGE, PUSH_FREQ_2HZ) != 0) {
        return DJI_RSDK_CONNECT_SUBSCRIBE_FAILED;
    }
    return DJI_RSDK_CONNECT_READY;
}

int dji_rs_sdk_toggle_recording(void)
{
    return dji_rs_sdk_set_recording(!is_camera_recording());
}

int dji_rs_sdk_set_recording(bool recording)
{
    camera_snapshot_t snapshot;
    dji_rs_sdk_get_snapshot(&snapshot);
    if (connect_logic_get_state() != PROTOCOL_CONNECTED || !camera_status_initialized || !snapshot.valid) {
        return -1;
    }
    if (snapshot.recording == recording) return 0;
    record_control_response_frame_t *response = recording
        ? command_logic_start_record()
        : command_logic_stop_record();
    int result = response == NULL ? -2 : response->ret_code;
    free(response);
    portENTER_CRITICAL(&s_snapshot_mux);
    s_snapshot.valid = false; // Wait for a new camera report after the command/timeout.
    portEXIT_CRITICAL(&s_snapshot_mux);
    return result;
}

void dji_rs_sdk_get_snapshot(camera_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_snapshot_mux);
    *snapshot = s_snapshot;
    snapshot->valid = snapshot->valid &&
        (TickType_t)(xTaskGetTickCount() - s_snapshot_tick) < pdMS_TO_TICKS(5000);
    snapshot->parameters_valid = snapshot->parameters_valid &&
        (TickType_t)(xTaskGetTickCount() - s_parameters_tick) < pdMS_TO_TICKS(5000);
    portEXIT_CRITICAL(&s_snapshot_mux);
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) snapshot->valid = false;
    if (!snapshot->valid) {
        snapshot->record_time_valid = snapshot->remain_time_valid = false;
        snapshot->remain_capacity_valid = snapshot->parameters_valid = false;
    }
}
