#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "camera/camera_types.h"

typedef struct {
    int mode_bit;
    int failsafe_bit;
} bf_mode_map_t;

typedef struct {
    bool active;
    bool failsafe;
} bf_mode_status_t;

typedef enum { BF_RECORD_NONE, BF_RECORD_START, BF_RECORD_STOP } bf_record_action_t;

typedef struct {
    bool sample_valid;
    bool candidate;
    bool stable;
    bool low_seen;
    bool pending;
    uint32_t since_ms;
} bf_record_policy_t;

bool bf_api_supported(const uint8_t *payload, size_t length);
bool bf_mode_map_parse(bf_mode_map_t *map, uint8_t permanent_id,
                       const uint8_t *payload, size_t length);
bool bf_mode_status_parse(const bf_mode_map_t *map, const uint8_t *payload,
                          size_t length, bf_mode_status_t *status);
bf_record_action_t bf_record_policy_step(bf_record_policy_t *policy, uint32_t now_ms,
                                         bool input_valid, bool active,
                                         const camera_controller_state_t *camera);
void bf_camera_osd(const camera_controller_state_t *camera, char lines[2][17]);
