#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "camera/camera_telemetry.h"
#include "betaflight/bf_control.h"
#include "ui/status_indicator.h"

int main(void)
{
    rr_settings_t settings = rr_settings_defaults(40);
    settings.low_battery_percent = 20; settings.low_remaining_seconds = 60;
    camera_telemetry_t m = {0};
    camera_controller_state_t s = {.protocol = CAMERA_PROTOCOL_DJI_ACTION2, .phase = CAMERA_PHASE_READY,
        .recording_valid = true, .recording = true};
    camera_telemetry_apply(&m, 0, &settings, &s);
    assert(!s.snapshot.record_time_valid); /* Booting into REC has no known start. */
    s.recording = false; camera_telemetry_apply(&m, 1, &settings, &s);
    s.recording = true; camera_telemetry_apply(&m, 2, &settings, &s);
    camera_telemetry_apply(&m, 2002, &settings, &s);
    assert(s.snapshot.record_time_valid && s.snapshot.record_time_estimated && s.snapshot.record_time == 2);
    s.recording_valid = false; camera_telemetry_apply(&m, 2500, &settings, &s);
    assert(!s.snapshot.record_time_valid);
    s.recording_valid = true; camera_telemetry_apply(&m, 7002, &settings, &s);
    assert(!s.snapshot.record_time_valid); /* No phantom timer across long unknown state. */
    s.recording = false; camera_telemetry_apply(&m, UINT32_MAX - 20, &settings, &s);
    s.recording = true; camera_telemetry_apply(&m, UINT32_MAX - 10, &settings, &s);
    camera_telemetry_apply(&m, 989, &settings, &s);
    assert(s.snapshot.record_time == 1 && s.snapshot.record_time_valid);
    s.phase = CAMERA_PHASE_RECONNECTING; camera_telemetry_apply(&m, 990, &settings, &s);
    s.phase = CAMERA_PHASE_READY; camera_telemetry_apply(&m, 991, &settings, &s);
    assert(!s.snapshot.record_time_valid);

    s = (camera_controller_state_t){.protocol = CAMERA_PROTOCOL_GOPRO, .phase = CAMERA_PHASE_READY,
        .recording_valid = true, .battery_valid = true, .snapshot = {.valid = true,
        .battery = 20, .remain_time_valid = true, .remain_time = 60,
        .storage_valid = true, .storage = 2, .mode_valid = true, .video_mode = false,
        .temperature_valid = true, .temperature = 1}};
    camera_telemetry_apply(&m, 1000, &settings, &s);
    assert(s.warnings == (CAMERA_WARN_SD_MISSING | CAMERA_WARN_MODE | CAMERA_WARN_HOT |
                          CAMERA_WARN_LOW_TIME | CAMERA_WARN_LOW_BATTERY));
    char lines[4][17]; bf_osd_state_t osd = {0};
    bf_camera_osd_configured(&s, &settings, &osd, lines);
    assert(!strcmp(lines[0], "!NO SD CARD") && status_indicator_select(&s) == INDICATOR_UNCERTAIN);
    s.record_result = CAMERA_RECORD_STOP_FAILED;
    bf_camera_osd_configured(&s, &settings, &osd, lines);
    assert(!strcmp(lines[0], "!NOT STOPPED"));
    s.record_result = CAMERA_RECORD_OK;
    s.snapshot.storage = 0; s.snapshot.video_mode = true;
    settings.heads_up_warnings = false;
    s.snapshot.battery = 22; s.snapshot.remain_time = 69;
    camera_telemetry_apply(&m, 1001, &settings, &s);
    assert(s.warnings == (CAMERA_WARN_LOW_TIME | CAMERA_WARN_LOW_BATTERY));
    s.snapshot.battery = 23; s.snapshot.remain_time = 70;
    s.snapshot.temperature = 2;
    camera_telemetry_apply(&m, 1002, &settings, &s);
    assert(s.warnings == CAMERA_WARN_HOT); /* Blocking heat cannot be hidden. */
    s.snapshot.valid = false; s.battery_valid = false;
    camera_telemetry_apply(&m, 1003, &settings, &s);
    assert(s.warnings == 0);
    s.snapshot.valid = true; s.snapshot.temperature_valid = false;
    s.snapshot.storage_valid = false; s.snapshot.storage = 1;
    camera_telemetry_apply(&m, 1004, &settings, &s);
    assert(s.warnings == 0); /* Unsupported fields never fabricate warnings. */
    rr_osd_layout_t layout = {.version = 1, .rows = {"TEXT {elapsed}", "{mode} {format}", "{battery}", "{space}"}};
    settings.osd_lines = 4; settings.owns_extra_lines = true;
    s.snapshot.record_time_valid = true; s.snapshot.record_time = 75;
    s.snapshot.parameters_valid = true; strcpy(s.snapshot.parameters, "4K 60");
    strcpy(s.snapshot.mode, "VIDEO"); s.snapshot.mode_valid = true;
    assert(rr_osd_layout_valid(&layout));
    bf_camera_osd_layout(&s, &settings, &osd, &layout, lines);
    assert(!strcmp(lines[0], "TEXT 01:15") && !strcmp(lines[1], "VIDEO 4K 60"));
    s.warnings = CAMERA_WARN_SD_FULL;
    bf_camera_osd_layout(&s, &settings, &osd, &layout, lines);
    assert(!strcmp(lines[0], "!SD FULL"));
    s.warnings = 0; s.phase = CAMERA_PHASE_RECONNECTING;
    bf_camera_osd_layout(&s, &settings, &osd, &layout, lines);
    assert(!strcmp(lines[0], "TEXT --"));
    strcpy(layout.rows[0], "{invalid}"); assert(!rr_osd_layout_valid(&layout));
    strcpy(layout.rows[0], "{state"); assert(!rr_osd_layout_valid(&layout));
    memset(layout.rows[0], 'X', 64); assert(!rr_osd_layout_valid(&layout));
    memset(layout.rows[0], 'X', 63); layout.rows[0][63] = 0;
    bf_camera_osd_layout(&s, &settings, &osd, &layout, lines);
    assert(strlen(lines[0]) == 16);
    settings.link_paused = true;
    bf_camera_osd_layout(&s, &settings, &osd, &layout, lines);
    assert(!lines[0][0] && !lines[3][0]);
    settings.link_paused = false;
    s = (camera_controller_state_t){.phase = CAMERA_PHASE_READY, .recording = true, .recording_valid = true,
        .snapshot = {.valid = true, .record_time_valid = true, .record_time_estimated = true, .record_time = 36000}};
    bf_camera_osd_configured(&s, &settings, &osd, lines);
    assert(strstr(lines[0], "~10:00:00") != NULL); /* Estimate marker cannot truncate the final second digit. */
    puts("Telemetry and OSD: timer, freshness, rollover, warning priority, templates, truncation and invalid input passed.");
    return 0;
}
