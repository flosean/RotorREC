#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Official EnumPresetGroup, not the similarly named icon IDs (0/1/2). */
#define GOPRO_PRESET_GROUP_VIDEO 1000U

/* Open GoPro BLE IDs; lengths on the wire do not include the packet header. */
enum {
    GOPRO_CMD_SHUTTER = 0x01,
    GOPRO_CMD_HARDWARE_INFO = 0x3c,
    GOPRO_CMD_IDENTIFY = 0x50,
    GOPRO_CMD_API_VERSION = 0x51,
    GOPRO_QUERY_STATUS = 0x13,
    GOPRO_SETTING_KEEPALIVE = 0x5b,
    GOPRO_STATUS_BUSY = 8,
    GOPRO_STATUS_ENCODING = 10,
    GOPRO_STATUS_BATTERY = 70,
    GOPRO_STATUS_READY = 82,
    GOPRO_STATUS_PRESET_GROUP = 96,
};
bool gopro_api_version_supported(const uint8_t *data, size_t length);

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
