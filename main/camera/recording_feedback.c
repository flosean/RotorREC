#include "recording_feedback.h"

void recording_feedback_request(recording_feedback_t *f, uint32_t now_ms,
                                bool target, bool accepted)
{
    /* A rejected second press must not replace an already running command. */
    if (!accepted && f->pending) return;
    f->target = target;
    f->target_valid = true;
    f->pending = accepted;
    f->saw_pending = false;
    f->requested_ms = now_ms;
    f->result = accepted ? (target ? CAMERA_RECORD_STARTING : CAMERA_RECORD_STOPPING)
                        : (target ? CAMERA_RECORD_START_FAILED : CAMERA_RECORD_STOP_FAILED);
}

void recording_feedback_apply(recording_feedback_t *f, uint32_t now_ms,
                              camera_controller_state_t *state)
{
    bool fresh = state->phase == CAMERA_PHASE_READY && state->recording_valid;
    bool settled = fresh && !state->command_pending && !state->saving;
    if (f->pending) {
        f->saw_pending |= state->command_pending;
        if (settled && state->recording == f->target) {
            f->pending = false;
            f->target_valid = false;
            f->result = CAMERA_RECORD_OK;
        } else if ((uint32_t)(now_ms - f->requested_ms) >= 12000 ||
                   (settled && f->saw_pending && state->last_command_result < 0)) {
            f->pending = false;
            f->result = !fresh ? CAMERA_RECORD_UNKNOWN :
                f->target ? CAMERA_RECORD_START_FAILED : CAMERA_RECORD_STOP_FAILED;
        }
    } else if (f->target_valid && settled) {
        if (state->recording == f->target) {
            f->target_valid = false;
            f->result = CAMERA_RECORD_OK;
        } else {
            f->result = f->target ? CAMERA_RECORD_START_FAILED : CAMERA_RECORD_STOP_FAILED;
        }
    } else if (settled && f->observed && f->was_recording && !state->recording) {
        /* Includes a stop at the camera itself; do not claim its cause is known. */
        f->result = CAMERA_RECORD_UNEXPECTED_STOP;
    } else if (settled && state->recording) {
        f->result = CAMERA_RECORD_OK;
    }
    if (settled) {
        f->observed = true;
        f->was_recording = state->recording;
    }
    state->record_result = f->result;
    if (!fresh && (f->target_valid || (f->observed && f->was_recording)))
        state->record_result = CAMERA_RECORD_UNKNOWN;
    if (state->phase == CAMERA_PHASE_LINK_PAUSED) state->record_result = CAMERA_RECORD_OK;
}
