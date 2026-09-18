#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "betaflight/msp_v2.h"
#include "betaflight/osd_messages.h"
#include "betaflight/bf_control.h"
#include "ui/status_indicator.h"
#include "platform/status_led_color.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return false; } } while (0)

static size_t response(uint16_t command, const uint8_t *payload, size_t length,
                       uint8_t *frame, bool error)
{
    size_t size = msp_v2_build_request(command, payload, length, frame, MSP_V2_MAX_PAYLOAD + 9);
    frame[2] = error ? '!' : '>';
    return size;
}

static bool codec_tests(void)
{
    CHECK(betaflight_protocol_self_check());
    CHECK(msp_v2_crc8_dvb_s2(0, 0) == 0);
    uint8_t frame[MSP_V2_MAX_PAYLOAD + 9];
    uint8_t payload[MSP_V2_MAX_PAYLOAD];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    CHECK(msp_v2_build_request(1, NULL, 1, frame, sizeof(frame)) == 0);
    CHECK(msp_v2_build_request(1, payload, sizeof(payload), frame, 8) == 0);
    CHECK(betaflight_osd_build_custom_message(4, "BAD", frame, sizeof(frame)) == 0);
    size_t n = betaflight_osd_build_custom_message(1, "012345678901234567890", frame, sizeof(frame));
    CHECK(n == 27 && frame[8] == 8 && frame[9] == 16);
    msp_v2_parser_t parser = {0};
    msp_v2_reply_t reply = {0};
    n = response(0x3007, payload, sizeof(payload), frame, false);
    /* Arbitrary UART fragmentation is byte-wise; '$' within payload is not a reset. */
    for (size_t i = 0; i < n; ++i) CHECK(msp_v2_parser_feed(&parser, frame[i], &reply) == (i == n - 1));
    CHECK(reply.command == 0x3007 && reply.length == 256 && !reply.error);
    CHECK(memcmp(reply.payload, payload, sizeof(payload)) == 0);
    n = response(MSP_STATUS, NULL, 0, frame, true);
    for (size_t i = 0; i < n; ++i) CHECK(msp_v2_parser_feed(&parser, frame[i], &reply) == (i == n - 1));
    CHECK(reply.error && reply.length == 0);
    frame[n - 1] ^= 1;
    for (size_t i = 0; i < n; ++i) CHECK(!msp_v2_parser_feed(&parser, frame[i], &reply));
    const uint8_t noise[] = { 'x', '$', 'M', '>', 0, '$', 'X', '>', 0, 1, 0, 0xff, 0xff };
    for (size_t i = 0; i < sizeof(noise); ++i) CHECK(!msp_v2_parser_feed(&parser, noise[i], &reply));
    n = response(MSP_API_VERSION, (const uint8_t[]){0, 1, 47}, 3, frame, false);
    for (size_t i = 0; i < n; ++i) CHECK(msp_v2_parser_feed(&parser, frame[i], &reply) == (i == n - 1));
    CHECK(reply.command == MSP_API_VERSION);
    /* Truncated packet timeout reset, then two consecutive valid packets. */
    for (size_t i = 0; i < 5; ++i) CHECK(!msp_v2_parser_feed(&parser, frame[i], &reply));
    msp_v2_parser_reset(&parser);
    for (unsigned j = 0; j < 2; ++j)
        for (size_t i = 0; i < n; ++i) CHECK(msp_v2_parser_feed(&parser, frame[i], &reply) == (i == n - 1));
    /* Deterministic noise exercises bounds and resynchronisation. */
    unsigned random = 1;
    for (unsigned i = 0; i < 10000; ++i) {
        random = random * 1664525U + 1013904223U;
        (void)msp_v2_parser_feed(&parser, (uint8_t)(random >> 24), &reply);
        CHECK(parser.used < sizeof(parser.bytes));
    }
    return true;
}

static bool status_tests(void)
{
    CHECK(!bf_api_supported((const uint8_t[]){0, 1, 46}, 3));
    CHECK(bf_api_supported((const uint8_t[]){0, 1, 47}, 3));
    CHECK(bf_api_supported((const uint8_t[]){0, 1, 49}, 3));
    CHECK(!bf_api_supported((const uint8_t[]){0, 2, 0}, 3));
    CHECK(!bf_api_supported((const uint8_t[]){0, 1}, 2));
    bf_mode_map_t map;
    CHECK(bf_mode_map_parse(&map, 40, (const uint8_t[]){27, 0, 40}, 3));
    CHECK(map.mode_bit == 2 && map.failsafe_bit == 0);
    uint8_t status[36] = {0};
    status[6] = 4; status[16] = 30;
    bf_mode_status_t result;
    CHECK(bf_mode_status_parse(&map, status, 21, &result));
    CHECK(result.active && !result.failsafe);
    status[6] |= 1;
    CHECK(bf_mode_status_parse(&map, status, 21, &result) && result.failsafe);
    status[6] = 4; status[17] = 4;
    CHECK(bf_mode_status_parse(&map, status, 21, &result) && result.failsafe);
    CHECK(!bf_mode_status_parse(&map, status, 20, &result));
    CHECK(!bf_mode_map_parse(&map, 40, (const uint8_t[]){0, 27}, 2));
    CHECK(!bf_mode_map_parse(&map, 0, (const uint8_t[]){0, 27}, 2));
    CHECK(!bf_mode_map_parse(&map, 40, (const uint8_t[]){40, 27, 40}, 3));
    uint8_t ids[40];
    memset(ids, 255, sizeof(ids)); ids[35] = 40; ids[37] = 27;
    CHECK(bf_mode_map_parse(&map, 40, ids, sizeof(ids)));
    memset(status, 0, sizeof(status)); status[15] = 1; status[16] = 8; status[17] = 30;
    CHECK(bf_mode_status_parse(&map, status, 22, &result) && result.active && !result.failsafe);
    status[16] |= 32;
    CHECK(bf_mode_status_parse(&map, status, 22, &result) && result.failsafe);
    status[15] = 0;
    CHECK(!bf_mode_status_parse(&map, status, 21, &result));
    return true;
}

static camera_controller_state_t camera_ready(void)
{
    return (camera_controller_state_t) { .phase = CAMERA_PHASE_READY,
        .recording_valid = true, .battery_valid = true, .snapshot = { .battery = 53 } };
}

static bool policy_tests(void)
{
    camera_controller_state_t c = camera_ready();
    bf_record_policy_t p = {0};
    CHECK(bf_record_policy_step(&p, 0, true, true, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 1000, true, true, &c) == BF_RECORD_NONE);
    CHECK(!p.low_seen); // boot-high must never start or stop
    CHECK(bf_record_policy_step(&p, 1100, true, false, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 1300, true, false, &c) == BF_RECORD_NONE && p.low_seen);
    for (unsigned i = 0; i < 20; ++i) {
        uint32_t t = 1500 + i * 1000;
        c.recording = false;
        CHECK(bf_record_policy_step(&p, t, true, true, &c) == BF_RECORD_NONE);
        CHECK(bf_record_policy_step(&p, t + 199, true, true, &c) == BF_RECORD_NONE);
        CHECK(bf_record_policy_step(&p, t + 200, true, true, &c) == BF_RECORD_START);
        CHECK(bf_record_policy_step(&p, t + 300, true, true, &c) == BF_RECORD_NONE); // failed command not retried
        c.recording = true;
        CHECK(bf_record_policy_step(&p, t + 400, true, true, &c) == BF_RECORD_NONE);
        CHECK(bf_record_policy_step(&p, t + 500, true, false, &c) == BF_RECORD_NONE);
        CHECK(bf_record_policy_step(&p, t + 700, true, false, &c) == BF_RECORD_STOP);
        CHECK(bf_record_policy_step(&p, t + 800, true, false, &c) == BF_RECORD_NONE);
    }
    CHECK(bf_record_policy_step(&p, 22000, false, false, &c) == BF_RECORD_NONE && !p.low_seen);
    CHECK(bf_record_policy_step(&p, 22100, true, false, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 22400, true, false, &c) == BF_RECORD_NONE); // reconnect-low doesn't stop existing rec
    c.recording = false; c.recording_valid = false;
    CHECK(bf_record_policy_step(&p, 22500, true, true, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 22800, true, true, &c) == BF_RECORD_NONE && p.pending);
    c.recording_valid = true; c.command_pending = true;
    CHECK(bf_record_policy_step(&p, 22900, true, true, &c) == BF_RECORD_NONE);
    c.command_pending = false; c.saving = true;
    CHECK(bf_record_policy_step(&p, 23000, true, true, &c) == BF_RECORD_NONE);
    c.saving = false;
    CHECK(bf_record_policy_step(&p, 23100, true, true, &c) == BF_RECORD_START);
    /* Don't stop merely because FC data/failsafe becomes invalid. */
    c.recording = true;
    CHECK(bf_record_policy_step(&p, 23200, false, false, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 23300, true, true, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 23600, true, true, &c) == BF_RECORD_NONE && !p.low_seen);
    /* Unsigned tick rollover still debounces correctly. */
    p = (bf_record_policy_t){0};
    CHECK(bf_record_policy_step(&p, UINT32_MAX - 100, true, false, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 100, true, false, &c) == BF_RECORD_NONE && p.low_seen);
    CHECK(bf_record_policy_step(&p, 200, true, true, &c) == BF_RECORD_NONE);
    CHECK(bf_record_policy_step(&p, 500, true, true, &c) == BF_RECORD_NONE); // already recording, no command
    return true;
}

static bool osd_tests(void)
{
    camera_controller_state_t c = camera_ready();
    char lines[2][17];
    bf_camera_osd(&c, lines);
    CHECK(strcmp(lines[0], "CAM STANDBY") == 0 && strcmp(lines[1], "CAM BAT 53%") == 0);
    c.recording = true;
    bf_camera_osd(&c, lines); CHECK(strcmp(lines[0], "CAM REC") == 0);
    c.saving = true;
    bf_camera_osd(&c, lines); CHECK(strcmp(lines[0], "CAM BUSY") == 0);
    c.recording_valid = false; c.battery_valid = false;
    bf_camera_osd(&c, lines);
    CHECK(strcmp(lines[0], "CAM STATUS ?") == 0 && strcmp(lines[1], "CAM BAT --") == 0);
    c.phase = CAMERA_PHASE_RECONNECTING; c.battery_valid = true;
    bf_camera_osd(&c, lines);
    CHECK(strcmp(lines[0], "CAM NO LINK") == 0 && strcmp(lines[1], "CAM BAT --") == 0);
    c = camera_ready(); c.snapshot.battery = 255;
    bf_camera_osd(&c, lines); CHECK(strcmp(lines[1], "CAM BAT --") == 0);
    c.snapshot.battery = 0;
    bf_camera_osd(&c, lines); CHECK(strcmp(lines[1], "CAM BAT 0%") == 0);
    c.snapshot.battery = 100;
    bf_camera_osd(&c, lines); CHECK(strcmp(lines[1], "CAM BAT 100%") == 0);
    return true;
}

static bool indicator_tests(void)
{
    /* User's C3 LED displays GRB green as red and GRB red as green.
       Test the actual driver packing, not just logical color selection. */
    CHECK(status_led_wire_color(12, 0, 0) == 0x0c0000);
    CHECK(status_led_wire_color(0, 12, 0) == 0x000c00);
    CHECK(status_led_wire_color(0, 0, 12) == 0x00000c);
    CHECK(status_led_wire_color(12, 8, 0) == 0x0c0800);
    camera_controller_state_t c = camera_ready();
    CHECK(status_indicator_select(&c) == INDICATOR_CONNECTED);
    c.recording = true;
    CHECK(status_indicator_select(&c) == INDICATOR_RECORDING);
    c.recording_valid = false;
    CHECK(status_indicator_select(&c) == INDICATOR_UNCERTAIN);
    c.recording_valid = true; c.saving = true;
    CHECK(status_indicator_select(&c) == INDICATOR_UNCERTAIN);
    c.saving = false; c.command_pending = true;
    CHECK(status_indicator_select(&c) == INDICATOR_UNCERTAIN);
    const camera_phase_t pairing[] = { CAMERA_PHASE_SCANNING, CAMERA_PHASE_VERIFYING,
        CAMERA_PHASE_ACTION2_SESSION, CAMERA_PHASE_ACTION2_PAIRING, CAMERA_PHASE_RECONNECTING };
    for (unsigned i = 0; i < sizeof(pairing) / sizeof(pairing[0]); ++i) {
        c.phase = pairing[i];
        CHECK(status_indicator_select(&c) == INDICATOR_PAIRING);
    }
    c.phase = CAMERA_PHASE_SCAN_FAILED;
    CHECK(status_indicator_select(&c) == INDICATOR_DISCONNECTED);
    c.phase = CAMERA_PHASE_PAIR_REJECTED;
    CHECK(status_indicator_select(&c) == INDICATOR_DISCONNECTED);
    c.phase = CAMERA_PHASE_WAITING_STATUS;
    CHECK(status_indicator_select(&c) == INDICATOR_UNCERTAIN);
    for (uint32_t t = 0; t < 4000; t += 50) {
        indicator_rgb_t p = status_indicator_pixel(INDICATOR_CONNECTED, t);
        CHECK(p.green == 12 && p.red == 0 && p.blue == 0);
        p = status_indicator_pixel(INDICATOR_RECORDING, t);
        CHECK(p.green == ((t % 2000) < 1000 ? 12 : 0) && p.red == 0);
        p = status_indicator_pixel(INDICATOR_DISCONNECTED, t);
        CHECK(p.red == ((t % 2000) < 1000 ? 12 : 0) && p.green == 0);
        p = status_indicator_pixel(INDICATOR_PAIRING, t);
        CHECK(p.red == ((t % 400) < 200 ? 12 : 0) && p.green == 0);
        p = status_indicator_pixel(INDICATOR_UNCERTAIN, t);
        CHECK(p.red == ((t % 400) < 200 ? 12 : 0) &&
              p.green == ((t % 400) < 200 ? 8 : 0) && p.blue == 0);
    }
    return true;
}

int main(void)
{
    if (!codec_tests() || !status_tests() || !policy_tests() || !osd_tests() || !indicator_tests()) return 1;
    printf("PASS: %u checks (codec, mode mapping, failsafe, 20 switch cycles, OSD, LED patterns)\n", checks);
    return 0;
}
