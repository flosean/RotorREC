#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "camera/recording_feedback.h"
#include "betaflight/bf_control.h"
#include "ui/status_indicator.h"

int main(void)
{
    recording_feedback_t f = {0};
    camera_controller_state_t c = {.phase = CAMERA_PHASE_READY, .recording_valid = true};
    recording_feedback_apply(&f, 0, &c);
    assert(c.record_result == CAMERA_RECORD_OK);
    recording_feedback_request(&f, 10, true, true);
    c.command_pending = true;
    recording_feedback_apply(&f, 20, &c);
    assert(c.record_result == CAMERA_RECORD_STARTING);
    /* An ACK or finished task alone does not confirm recording. */
    c.command_pending = false;
    recording_feedback_apply(&f, 100, &c);
    assert(c.record_result == CAMERA_RECORD_STARTING);
    recording_feedback_apply(&f, 12010, &c);
    assert(c.record_result == CAMERA_RECORD_START_FAILED);
    char lines[4][17];
    rr_settings_t settings = rr_settings_defaults(40);
    settings.osd_lines = 4; settings.owns_extra_lines = true;
    bf_osd_state_t osd = {0};
    bf_camera_osd_configured(&c, &settings, &osd, lines);
    assert(!strcmp(lines[0], "!REC FAILED"));
    assert(status_indicator_select(&c) == INDICATOR_UNCERTAIN);
    /* A late real confirmation clears failure. */
    c.recording = true;
    recording_feedback_apply(&f, 13000, &c);
    assert(c.record_result == CAMERA_RECORD_OK);
    recording_feedback_request(&f, 14000, false, true);
    c.command_pending = true;
    recording_feedback_apply(&f, 14001, &c);
    recording_feedback_request(&f, 14002, true, false);
    assert(!f.target && f.pending); /* second rejected press does not overwrite STOP */
    c.command_pending = false; c.last_command_result = -3;
    recording_feedback_apply(&f, 14500, &c);
    assert(c.record_result == CAMERA_RECORD_STOP_FAILED);
    c.snapshot.valid = c.snapshot.record_time_valid = true;
    bf_camera_osd_configured(&c, &settings, &osd, lines);
    assert(!strcmp(lines[0], "!NOT STOPPED")); /* Timer must not hide the error. */
    c.phase = CAMERA_PHASE_RECONNECTING; c.recording_valid = false;
    recording_feedback_apply(&f, 15000, &c);
    assert(c.record_result == CAMERA_RECORD_UNKNOWN);
    c.phase = CAMERA_PHASE_READY; c.recording_valid = true; c.recording = false;
    recording_feedback_apply(&f, 16000, &c);
    assert(c.record_result == CAMERA_RECORD_OK);
    c.recording = true;
    recording_feedback_apply(&f, 17000, &c);
    c.recording = false;
    recording_feedback_apply(&f, 18000, &c);
    assert(c.record_result == CAMERA_RECORD_UNEXPECTED_STOP);
    recording_feedback_apply(&f, 18001, &c);
    assert(c.record_result == CAMERA_RECORD_UNEXPECTED_STOP);
    /* Tick rollover, unknown while disconnected, then truthful failure on reconnect. */
    f = (recording_feedback_t){0};
    recording_feedback_request(&f, UINT32_MAX - 5999, true, true);
    c.phase = CAMERA_PHASE_RECONNECTING; c.recording_valid = false;
    recording_feedback_apply(&f, 6000, &c);
    assert(!f.pending && c.record_result == CAMERA_RECORD_UNKNOWN);
    c.phase = CAMERA_PHASE_READY; c.recording_valid = true; c.last_command_result = 0;
    recording_feedback_apply(&f, 6001, &c);
    assert(c.record_result == CAMERA_RECORD_START_FAILED);
    /* A real stop confirmed after saving succeeds; no unexpected-stop warning. */
    f = (recording_feedback_t){0}; c.recording = true;
    recording_feedback_apply(&f, 0, &c);
    recording_feedback_request(&f, 1, false, true);
    c.recording = false; c.saving = true;
    recording_feedback_apply(&f, 2, &c);
    assert(c.record_result == CAMERA_RECORD_STOPPING);
    c.saving = false;
    recording_feedback_apply(&f, 3, &c);
    recording_feedback_apply(&f, 4, &c);
    assert(c.record_result == CAMERA_RECORD_OK);
    puts("Recording feedback: confirmation, failure, disconnect, late recovery, stop, rollover and OSD passed.");
    return 0;
}
