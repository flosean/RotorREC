#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "camera/gopro/gopro_protocol.h"
#include "betaflight/bf_control.h"

static void packet_tests(void)
{
    gopro_rx_t rx = {0};
    const uint8_t ack[] = {2, 1, 0};
    assert(gopro_rx_feed(&rx, ack, sizeof(ack)) == 1);
    assert(rx.used == 2 && rx.data[0] == 1 && rx.data[1] == 0);
    const uint8_t first[] = {0x20, 6, 0x13, 0, 8};
    const uint8_t last[] = {0x80, 1, 0, 10};
    assert(gopro_rx_feed(&rx, first, sizeof(first)) == 0);
    assert(gopro_rx_feed(&rx, last, sizeof(last)) == 1);
    assert(rx.used == 6 && rx.data[5] == 10);
    assert(gopro_rx_feed(&rx, last, sizeof(last)) == -1); /* orphan */
    assert(gopro_rx_feed(&rx, first, sizeof(first)) == 0);
    const uint8_t out_of_order[] = {0x81, 1};
    assert(gopro_rx_feed(&rx, out_of_order, sizeof(out_of_order)) == -1);
    const uint8_t oversized[] = {0x22, 1, 0}; /* 513 > capacity */
    assert(gopro_rx_feed(&rx, oversized, sizeof(oversized)) == -1);
    const uint8_t extended16[] = {0x40, 0, 2, 1, 0};
    assert(gopro_rx_feed(&rx, extended16, sizeof(extended16)) == 1);
    const uint8_t overflow[] = {1, 1, 0};
    assert(gopro_rx_feed(&rx, overflow, sizeof(overflow)) == -1);
    assert(gopro_rx_feed(&rx, NULL, 0) == -1);
    const uint8_t short_header[] = {0x20};
    assert(gopro_rx_feed(&rx, short_header, sizeof(short_header)) == -1);
    const uint8_t reserved[] = {0x60, 1, 0};
    assert(gopro_rx_feed(&rx, reserved, sizeof(reserved)) == -1);
    /* A new start packet replaces an unfinished reply. */
    assert(gopro_rx_feed(&rx, first, sizeof(first)) == 0);
    assert(gopro_rx_feed(&rx, ack, sizeof(ack)) == 1);
    const uint8_t wrap_start[] = {0x20, 19, 42};
    assert(gopro_rx_feed(&rx, wrap_start, sizeof(wrap_start)) == 0);
    for (unsigned i = 0; i < 18; ++i) {
        uint8_t part[] = {(uint8_t)(0x80 | (i & 15)), (uint8_t)i};
        assert(gopro_rx_feed(&rx, part, sizeof(part)) == (i == 17 ? 1 : 0));
    }
    assert(rx.data[18] == 17);
    gopro_rx_t other = {0};
    assert(gopro_rx_feed(&rx, first, sizeof(first)) == 0);
    assert(gopro_rx_feed(&other, ack, sizeof(ack)) == 1);
    assert(gopro_rx_feed(&rx, last, sizeof(last)) == 1);
}

static void status_tests(void)
{
    /* Open GoPro 0x13 TLVs: encoding, busy, battery %, ready, preset group. */
    uint8_t reply[] = {0x13, 0, 10, 1, 1, 8, 1, 0, 70, 1, 84,
                      82, 1, 1, 96, 4, 0, 0, 0x03, 0xe8};
    gopro_status_t status = {0};
    assert(gopro_parse_status(reply, sizeof(reply), &status));
    assert(status.encoding && !status.busy && status.ready &&
           status.battery == 84 && status.preset_group == GOPRO_PRESET_GROUP_VIDEO);
    for (size_t i = 0; i < sizeof(reply); ++i)
        assert(!gopro_parse_status(reply, i, &status));
    reply[10] = 101;
    assert(!gopro_parse_status(reply, sizeof(reply), &status));
    assert(status.battery == 84); /* malformed data must not partially overwrite */
    reply[10] = 84;
    reply[4] = 2;
    assert(!gopro_parse_status(reply, sizeof(reply), &status));
    reply[4] = 0;
    reply[1] = 1;
    assert(!gopro_parse_status(reply, sizeof(reply), &status));
    reply[1] = 0;
    reply[19] = 0xe9;
    assert(gopro_parse_status(reply, sizeof(reply), &status));
    assert(status.preset_group == 1001 && !status.encoding);
    uint8_t extra[23];
    memcpy(extra, reply, sizeof(reply));
    memcpy(extra + sizeof(reply), (uint8_t[]){70, 1, 30}, 3);
    assert(!gopro_parse_status(extra, sizeof(extra), &status));
    extra[20] = 200; /* unknown TLVs can be skipped */
    assert(gopro_parse_status(extra, sizeof(extra), &status));
    extra[21] = 4;
    assert(!gopro_parse_status(extra, sizeof(extra), &status));
    const uint8_t shutter_ack[] = {1, 0};
    assert(!gopro_parse_status(shutter_ack, sizeof(shutter_ack), &status));
}

static void advertisement_tests(void)
{
    const uint8_t adv[] = {2, 1, 6, 5, 2, 0x0a, 0x18, 0xa6, 0xfe};
    assert(gopro_advertisement(adv, sizeof(adv)));
    assert(!gopro_advertisement(adv, sizeof(adv) - 1));
    const uint8_t unrelated[] = {3, 3, 0xf0, 0xff};
    assert(!gopro_advertisement(unrelated, sizeof(unrelated)));
}

static void bridge_tests(void)
{
    camera_controller_state_t camera = {
        .protocol = CAMERA_PROTOCOL_GOPRO, .phase = CAMERA_PHASE_READY,
        .recording_valid = true, .battery_valid = true, .snapshot.battery = 84,
    };
    char lines[2][17];
    bf_camera_osd(&camera, lines);
    assert(strcmp(lines[0], "CAM STANDBY") == 0);
    assert(strcmp(lines[1], "CAM BAT 84%") == 0);
    bf_record_policy_t policy = {0};
    assert(bf_record_policy_step(&policy, 0, true, false, &camera) == BF_RECORD_NONE);
    assert(bf_record_policy_step(&policy, 200, true, false, &camera) == BF_RECORD_NONE);
    assert(bf_record_policy_step(&policy, 300, true, true, &camera) == BF_RECORD_NONE);
    assert(bf_record_policy_step(&policy, 500, true, true, &camera) == BF_RECORD_START);
    camera.recording = true;
    bf_camera_osd(&camera, lines);
    assert(strcmp(lines[0], "CAM REC") == 0);
    camera.recording_valid = false;
    bf_camera_osd(&camera, lines);
    assert(strcmp(lines[0], "CAM STATUS ?") == 0);
    camera.phase = CAMERA_PHASE_RECONNECTING;
    bf_camera_osd(&camera, lines);
    assert(strcmp(lines[0], "CAM NO LINK") == 0 && strcmp(lines[1], "CAM BAT --") == 0);
}

int main(void)
{
    packet_tests(); status_tests(); advertisement_tests(); bridge_tests();
    puts("GoPro packet, status, advertisement and Betaflight integration tests passed");
    return 0;
}
