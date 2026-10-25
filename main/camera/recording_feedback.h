#pragma once
#include "camera_types.h"

/* Observes commands and fresh camera state; never sends or retries a shutter. */
typedef struct {
    bool pending;
    bool target;
    bool target_valid;
    bool saw_pending;
    bool observed;
    bool was_recording;
    uint32_t requested_ms;
    camera_record_result_t result;
} recording_feedback_t;

void recording_feedback_request(recording_feedback_t *feedback, uint32_t now_ms,
                                bool target, bool accepted);
void recording_feedback_apply(recording_feedback_t *feedback, uint32_t now_ms,
                              camera_controller_state_t *state);
