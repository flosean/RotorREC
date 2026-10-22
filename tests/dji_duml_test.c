#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "camera/dji/dji_duml.h"

/* Independent captured Action 2 remote query, already used by the verified adapter. */
static const uint8_t captured[] = {
    0x55, 0x0e, 0x04, 0x66, 0x53, 0x01, 0x04, 0x00,
    0x20, 0x02, 0x70, 0x01, 0x02, 0xb1,
};
static unsigned received;
static void capture(const uint8_t *data, size_t length)
{
    duml_frame_t frame;
    assert(duml_parse(data, length, &frame));
    ++received;
}

int main(void)
{
    uint8_t buffer[1023], payload[1010] = {0};
    duml_frame_t frame;
    assert(duml_build(buffer, sizeof(buffer), 0x53, 1, 0x0400,
        0x20, 2, 0x70, (uint8_t[]){1}, 1) == sizeof(captured));
    assert(!memcmp(buffer, captured, sizeof(captured)));
    assert(duml_parse(buffer, sizeof(captured), &frame));
    assert(frame.sequence == 0x0400 && frame.command_set == 2 && frame.command_id == 0x70);
    for (size_t split = 0; split <= sizeof(captured); ++split) {
        duml_rx_t rx = {0}; received = 0;
        duml_rx_feed(&rx, captured, split, capture);
        duml_rx_feed(&rx, captured + split, sizeof(captured) - split, capture);
        assert(received == 1 && rx.used == 0);
    }
    duml_rx_t rx = {0}; received = 0;
    memcpy(buffer, captured, sizeof(captured));
    memcpy(buffer + sizeof(captured), captured, sizeof(captured));
    duml_rx_feed(&rx, buffer, sizeof(captured) * 2, capture);
    assert(received == 2 && rx.used == 0);
    assert(!duml_parse(buffer, sizeof(captured) * 2, &frame));
    /* CRC damage, noise and an invalid header must resynchronize. */
    buffer[sizeof(captured) - 1] ^= 1;
    received = 0;
    duml_rx_feed(&rx, (uint8_t[]){0, 0xff, 0x55, 0, 0, 0}, 6, capture);
    duml_rx_feed(&rx, buffer, sizeof(captured) * 2, capture);
    assert(received == 1 && rx.used == 0);
    assert(duml_build(buffer, sizeof(buffer), 2, 7, 0xffff, 0x40, 7, 0x45,
        payload, sizeof(payload)) == sizeof(buffer));
    received = 0;
    for (size_t i = 0; i < sizeof(buffer); ++i) duml_rx_feed(&rx, buffer + i, 1, capture);
    assert(received == 1 && rx.used == 0);
    assert(!duml_build(buffer, sizeof(buffer), 2, 7, 0, 0, 7, 0x45, payload, SIZE_MAX));
    assert(!duml_build(buffer, 13, 2, 7, 0, 0, 7, 0x45, payload, 1));
    puts("Action 2 captured wire bytes, DUML fragmentation, concatenation, CRC recovery and bounds passed.");
    return 0;
}
