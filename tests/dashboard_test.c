#include <string.h>
#include "../main/ui/dashboard.c"
int main(void)
{
    dashboard_init();
    camera_controller_state_t state = {.phase = CAMERA_PHASE_READY, .protocol = CAMERA_PROTOCOL_DJI_RSDK,
        .recording = true, .snapshot = {.valid = true, .recording = true}};
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "STATUS UNKNOWN")); // Photo/shutter data is not a video REC.
    state.recording = false; state.snapshot.recording = false;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "STATUS UNKNOWN")); // Unknown is not standby either.
    state.recording_valid = true; state.command_pending = true;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "WAITING CAMERA..."));
    state.command_pending = false; state.saving = true;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "CAMERA BUSY"));
    state.saving = false; state.recording = true;
    state.snapshot.record_time_valid = true; state.snapshot.record_time = 12;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "REC  00:12"));
    state.recording = false;
    dashboard_refresh(&state);
    assert(strstr(s_record_label->text, "STANDBY"));
    state.snapshot.valid = false;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "--  NOT READY  --"));
    state.phase = CAMERA_PHASE_LINK_PAUSED;
    dashboard_refresh(&state);
    assert(!strcmp(s_record_label->text, "CAMERA LINK DISABLED"));
    puts("LCD text: unknown/photo, pending, busy, confirmed recording, stale and paused states passed.");
    return 0;
}
