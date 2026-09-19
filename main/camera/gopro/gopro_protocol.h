#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Official EnumPresetGroup, not the similarly named icon IDs (0/1/2). */
#define GOPRO_PRESET_GROUP_VIDEO 1000U

/* Open GoPro BLE packetization. Each response characteristic needs its own RX. */
typedef struct {
    uint8_t data[512];
    size_t expected;
    size_t used;
    uint8_t sequence;
} gopro_rx_t;

/* -1: malformed, 0: incomplete, 1: complete (data/used valid until next call). */
int gopro_rx_feed(gopro_rx_t *rx, const uint8_t *packet, size_t length);

typedef struct {
    bool encoding;
    bool busy;
    bool ready;
    uint8_t battery;
    uint32_t preset_group;
} gopro_status_t;

/* Accept a complete successful 0x13 reply with every required status present. */
bool gopro_parse_status(const uint8_t *data, size_t length, gopro_status_t *status);
bool gopro_advertisement(const uint8_t *data, size_t length);
