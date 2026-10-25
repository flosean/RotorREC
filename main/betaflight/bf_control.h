#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "camera/camera_types.h"
#include "management/settings.h"

typedef struct {
    int mode_bit;
    int failsafe_bit;
    int arm_bit;
} bf_mode_map_t;

typedef struct {
    bool active;
    bool failsafe;
    bool armed;
    bool arm_valid;
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

typedef struct {
    bf_record_policy_t user;
    bool arm_sample_valid, arm_candidate, arm_initialized, armed;
    uint32_t arm_since_ms;
    bool intent_valid, target, stop_delay_active, attempted, manual_override;
    uint32_t stop_since_ms, attempted_ms;
} bf_automation_t;

bool bf_api_supported(const uint8_t *payload, size_t length);
bool bf_mode_map_parse(bf_mode_map_t *map, uint8_t permanent_id,
                       const uint8_t *payload, size_t length);
/* Read all 32-ID MSP_BOXIDS pages before exposing a complete mapping. */
bool bf_mode_map_read(bf_mode_map_t *map, uint8_t permanent_id,
                     bool (*read_page)(uint8_t page, uint8_t ids[32], size_t *length));
bool bf_mode_status_parse(const bf_mode_map_t *map, const uint8_t *payload,
                          size_t length, bf_mode_status_t *status);
bf_record_action_t bf_record_policy_step(bf_record_policy_t *policy, uint32_t now_ms,
                                         bool input_valid, bool active,
                                         const camera_controller_state_t *camera);
bf_record_action_t bf_automation_step(bf_automation_t *policy, uint32_t now_ms,
    bool input_valid, const bf_mode_status_t *mode, const rr_settings_t *settings,
    const camera_controller_state_t *camera);
void bf_automation_manual(bf_automation_t *policy, uint32_t now_ms, bool target);
void bf_automation_restore_stop(bf_automation_t *policy, uint32_t now_ms,
                                uint8_t remaining, const rr_settings_t *settings);
uint8_t bf_stop_delay_remaining(const bf_automation_t *policy, uint32_t now_ms,
                               const rr_settings_t *settings);
void bf_camera_osd(const camera_controller_state_t *camera, char lines[2][17]);
typedef struct { bool low_battery; } bf_osd_state_t;
void bf_camera_osd_configured(const camera_controller_state_t *camera,
                             const rr_settings_t *settings, bf_osd_state_t *state,
                             char lines[4][17]);
void bf_camera_osd_layout(const camera_controller_state_t *camera,
    const rr_settings_t *settings, bf_osd_state_t *state, const rr_osd_layout_t *layout,
    char lines[4][17]);
