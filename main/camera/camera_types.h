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
    bool record_time_valid;
    bool remain_time_valid;
    bool remain_capacity_valid;
    bool parameters_valid;
    bool recording;
    uint8_t battery;
    uint16_t record_time;
    uint32_t remain_time;
    uint32_t remain_capacity;
    char mode[21];
    char parameters[21];
} camera_snapshot_t;

typedef struct {
    camera_protocol_t protocol;
    camera_phase_t phase;
    uint16_t pairing_code;
    int last_command_result;
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
