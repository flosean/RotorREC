#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CAMERA_PROTOCOL_NONE,
    CAMERA_PROTOCOL_DJI_RSDK,
    CAMERA_PROTOCOL_DJI_ACTION2,
    CAMERA_PROTOCOL_GOPRO,
} camera_protocol_t;

typedef enum {
    CAMERA_PHASE_INITIALIZING,
    CAMERA_PHASE_IDLE,
    CAMERA_PHASE_SCANNING,
    CAMERA_PHASE_VERIFYING,
    CAMERA_PHASE_ACTION2_SESSION,
    CAMERA_PHASE_ACTION2_PAIRING,
    CAMERA_PHASE_WAITING_STATUS,
    CAMERA_PHASE_READY,
    CAMERA_PHASE_RECONNECTING,
    CAMERA_PHASE_SCAN_FAILED,
    CAMERA_PHASE_GATT_MISMATCH,
    CAMERA_PHASE_PAIR_REJECTED,
    CAMERA_PHASE_SUBSCRIBE_FAILED,
    CAMERA_PHASE_LINK_PAUSED,
} camera_phase_t;

typedef struct {
    bool valid;
    bool recording_valid; /* Fresh, recognized video mode and recording state. */
    bool record_time_valid;
    bool remain_time_valid;
    bool remain_capacity_valid;
    bool parameters_valid;
    bool mode_valid;
    bool video_mode;
    bool record_time_estimated;
    bool storage_valid;
    bool temperature_valid;
    bool cold_valid;
    bool write_speed_valid;
    uint8_t storage; /* 0 OK, 1 full, 2 missing, 3 format error, 4 busy, 8 changed. */
    uint8_t temperature; /* 0 normal, 1 warning, 2 blocked, 3 shutdown imminent. */
    bool cold;
    bool write_speed_error;
    bool recording;
    uint8_t battery;
    uint32_t record_time;
    uint32_t remain_time;
    uint32_t remain_capacity;
    char mode[21];
    char parameters[21];
} camera_snapshot_t;

enum {
    CAMERA_WARN_SD_MISSING = 1, CAMERA_WARN_SD_FULL = 2, CAMERA_WARN_SD_ERROR = 4,
    CAMERA_WARN_SD_SLOW = 8, CAMERA_WARN_MODE = 16, CAMERA_WARN_HOT = 32,
    CAMERA_WARN_COLD = 64, CAMERA_WARN_LOW_TIME = 128, CAMERA_WARN_LOW_BATTERY = 256,
};
static inline const char *camera_warning_message(uint16_t warnings)
{
    if (warnings & CAMERA_WARN_SD_MISSING) return "!NO SD CARD";
    if (warnings & CAMERA_WARN_SD_FULL) return "!SD FULL";
    if (warnings & CAMERA_WARN_SD_ERROR) return "!SD ERROR";
    if (warnings & CAMERA_WARN_SD_SLOW) return "!SD TOO SLOW";
    if (warnings & CAMERA_WARN_MODE) return "!SELECT VIDEO";
    if (warnings & CAMERA_WARN_HOT) return "!CAM TOO HOT";
    if (warnings & CAMERA_WARN_COLD) return "!CAM TOO COLD";
    if (warnings & CAMERA_WARN_LOW_TIME) return "!LOW REC TIME";
    if (warnings & CAMERA_WARN_LOW_BATTERY) return "!LOW CAM BAT";
    return "";
}

typedef enum {
    CAMERA_RECORD_OK,
    CAMERA_RECORD_STARTING,
    CAMERA_RECORD_STOPPING,
    CAMERA_RECORD_START_FAILED,
    CAMERA_RECORD_STOP_FAILED,
    CAMERA_RECORD_UNKNOWN,
    CAMERA_RECORD_UNEXPECTED_STOP,
} camera_record_result_t;

static inline const char *camera_record_message(camera_record_result_t result)
{
    switch (result) {
    case CAMERA_RECORD_STARTING: return "REC STARTING";
    case CAMERA_RECORD_STOPPING: return "REC STOPPING";
    case CAMERA_RECORD_START_FAILED: return "!REC FAILED";
    case CAMERA_RECORD_STOP_FAILED: return "!NOT STOPPED";
    case CAMERA_RECORD_UNKNOWN: return "!REC UNKNOWN";
    case CAMERA_RECORD_UNEXPECTED_STOP: return "!REC STOPPED";
    default: return "";
    }
}

typedef struct {
    camera_protocol_t protocol;
    camera_phase_t phase;
    uint16_t pairing_code;
    int last_command_result;
    camera_record_result_t record_result;
    uint32_t manual_command_sequence;
    bool manual_record_target;
    uint8_t stop_delay_remaining;
    bool stop_persistence_error;
    uint16_t warnings;
    bool command_pending;
    bool link_connected; /* Also true while pairing/waiting for status. */
    bool recording;
    bool recording_valid;
    bool saving;
    bool battery_valid;
    int8_t rssi;
    uint8_t address_type;
    uint16_t mtu;
    uint16_t gatt_attribute_count;
    uint32_t notification_count;
    bool target_service_found;
    camera_snapshot_t snapshot;
} camera_controller_state_t;
