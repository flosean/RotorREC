#include <assert.h>
#include <stdio.h>
#include "betaflight/bf_control.h"

int main(void)
{
    rr_settings_t s = rr_settings_defaults(40), decoded = s;
    assert(rr_control_decode(rr_control_encode(&s), &decoded));
    assert(decoded.record_mode == RR_RECORD_USER && decoded.stop_delay_seconds == 5);
    for (unsigned mode = RR_RECORD_USER; mode <= RR_RECORD_OFF; ++mode)
        for (unsigned delay = 0; delay <= 30; ++delay) {
            s.record_mode = (rr_record_mode_t)mode; s.stop_delay_seconds = (uint8_t)delay;
            s.low_remaining_seconds = 3600;
            assert(rr_control_decode(rr_control_encode(&s), &decoded));
            assert(decoded.record_mode == s.record_mode && decoded.stop_delay_seconds == delay &&
                   decoded.low_remaining_seconds == 3600);
        }
    assert(!rr_control_decode(0x02000000, &decoded));
    assert(!rr_control_decode(0x01800000, &decoded));
    assert(!rr_control_decode(0x01000007, &decoded));
    assert(!rr_control_decode(0x010000f8, &decoded));
    s = rr_settings_defaults(40); s.record_mode = RR_RECORD_ARM;
    bf_automation_t p = {0};
    bf_mode_status_t m = {.arm_valid = true};
    camera_controller_state_t c = {.phase = CAMERA_PHASE_READY, .recording_valid = true};
#define STEP(t) bf_automation_step(&p, (t), true, &m, &s, &c)
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    m.armed = true;
    assert(STEP(300) == BF_RECORD_NONE && STEP(499) == BF_RECORD_NONE);
    assert(STEP(500) == BF_RECORD_START);
    assert(STEP(600) == BF_RECORD_NONE); /* No blind retry after ACK/timeout. */
    assert(STEP(20499) == BF_RECORD_NONE && STEP(20500) == BF_RECORD_START);
    c.recording = true;
    m.armed = false;
    assert(STEP(21000) == BF_RECORD_NONE && STEP(21200) == BF_RECORD_NONE);
    assert(bf_stop_delay_remaining(&p, 21200, &s) == 5);
    assert(STEP(26000) == BF_RECORD_NONE);
    /* Re-arm arriving at the countdown boundary suppresses STOP while debouncing. */
    m.armed = true;
    assert(STEP(26200) == BF_RECORD_NONE && STEP(26400) == BF_RECORD_NONE);
    assert(!p.stop_delay_active);
    m.armed = false;
    assert(STEP(27000) == BF_RECORD_NONE && STEP(27200) == BF_RECORD_NONE);
    m.failsafe = true;
    assert(STEP(33000) == BF_RECORD_NONE); /* never STOP during failsafe */
    m.failsafe = false;
    assert(STEP(33100) == BF_RECORD_NONE);
    assert(STEP(33300) == BF_RECORD_STOP);
    c.recording = false;
    assert(STEP(33400) == BF_RECORD_NONE && !p.intent_valid);
    /* Boot while armed, camera connects late; Off and auto_record=false are respected. */
    p = (bf_automation_t){0}; m.armed = true; c.phase = CAMERA_PHASE_RECONNECTING;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    c.phase = CAMERA_PHASE_READY;
    assert(STEP(300) == BF_RECORD_START);
    bf_automation_manual(&p, 400, false);
    assert(STEP(400) == BF_RECORD_NONE && STEP(30000) == BF_RECORD_NONE);
    p = (bf_automation_t){0}; s.auto_record = false;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    s.record_mode = RR_RECORD_OFF;
    assert(STEP(300) == BF_RECORD_NONE);
    /* Button: LOW baseline, press/release/press; no deferred toggle on stale state. */
    p = (bf_automation_t){0}; s = rr_settings_defaults(40); s.record_mode = RR_RECORD_BUTTON;
    m.active = true;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    m.active = false; assert(STEP(300) == BF_RECORD_NONE && STEP(500) == BF_RECORD_NONE);
    m.active = true; assert(STEP(600) == BF_RECORD_NONE && STEP(800) == BF_RECORD_START);
    c.recording = true; assert(STEP(900) == BF_RECORD_NONE);
    m.active = false; assert(STEP(1000) == BF_RECORD_NONE && STEP(1200) == BF_RECORD_NONE);
    m.active = true; assert(STEP(1300) == BF_RECORD_NONE && STEP(1500) == BF_RECORD_STOP);
    c.recording = false; assert(STEP(1600) == BF_RECORD_NONE);
    /* COMBI manual stop remains stopped while armed; delay button cancels the stop. */
    p = (bf_automation_t){0}; s.record_mode = RR_RECORD_COMBI;
    m.armed = true; m.active = false;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_START);
    c.recording = true; assert(STEP(400) == BF_RECORD_NONE);
    m.active = true; assert(STEP(500) == BF_RECORD_NONE && STEP(700) == BF_RECORD_STOP);
    c.recording = false; assert(STEP(800) == BF_RECORD_NONE && STEP(30000) == BF_RECORD_NONE);
    m.active = false; assert(STEP(30100) == BF_RECORD_NONE && STEP(30300) == BF_RECORD_NONE);
    c.recording = true; m.armed = false;
    assert(STEP(30400) == BF_RECORD_NONE && STEP(30600) == BF_RECORD_NONE);
    m.active = true;
    assert(STEP(30700) == BF_RECORD_NONE && STEP(30900) == BF_RECORD_NONE);
    assert(!p.stop_delay_active && STEP(40000) == BF_RECORD_NONE);
    /* Millisecond rollover and zero delay. */
    p = (bf_automation_t){0}; s.record_mode = RR_RECORD_ARM; s.stop_delay_seconds = 0;
    m.armed = true;
    assert(STEP(UINT32_MAX - 300) == BF_RECORD_NONE && STEP(UINT32_MAX - 100) == BF_RECORD_NONE);
    m.armed = false;
    assert(STEP(UINT32_MAX - 50) == BF_RECORD_NONE && STEP(150) == BF_RECORD_STOP);
    /* A reboot resumes the saved remaining delay, never assumes FC disarmed. */
    p = (bf_automation_t){0}; s.stop_delay_seconds = 5; m.armed = false;
    bf_automation_restore_stop(&p, 1000, 2, &s);
    assert(STEP(1000) == BF_RECORD_NONE && STEP(1200) == BF_RECORD_NONE);
    assert(bf_stop_delay_remaining(&p, 1200, &s) == 2);
    assert(STEP(2999) == BF_RECORD_NONE && STEP(3000) == BF_RECORD_STOP);
    p = (bf_automation_t){0}; m.armed = true;
    bf_automation_restore_stop(&p, 4000, 0, &s);
    assert(STEP(4000) == BF_RECORD_NONE && STEP(4200) == BF_RECORD_NONE);
    assert(p.target && !p.stop_delay_active); /* Current ARM supersedes restored STOP. */
    /* Disabling automatic STOP keeps recording across DISARM independently of START. */
    p = (bf_automation_t){0}; s.auto_stop = false; m.armed = true; c.recording = true;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    m.armed = false;
    assert(STEP(300) == BF_RECORD_NONE && STEP(500) == BF_RECORD_NONE && STEP(60000) == BF_RECORD_NONE);
    assert(!p.stop_delay_active && !p.intent_valid);
    /* Maximum delay is honored at the exact boundary, not just accepted by the codec. */
    p = (bf_automation_t){0}; s.auto_stop = true; s.stop_delay_seconds = 30; m.armed = true;
    assert(STEP(0) == BF_RECORD_NONE && STEP(200) == BF_RECORD_NONE);
    m.armed = false;
    assert(STEP(300) == BF_RECORD_NONE && STEP(500) == BF_RECORD_NONE);
    assert(bf_stop_delay_remaining(&p, 500, &s) == 30);
    assert(STEP(30499) == BF_RECORD_NONE && STEP(30500) == BF_RECORD_STOP);
    puts("Recording automation: ARM, delay/rearm, failsafe, late connection, manual override, Button/Combi, persisted stop and rollover passed.");
    return 0;
}
