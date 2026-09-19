#include "gopro_protocol.h"

#include <string.h>

int gopro_rx_feed(gopro_rx_t *rx, const uint8_t *packet, size_t length)
{
    if (!packet || !length) goto invalid;
    size_t header = 1;
    uint8_t first = packet[0];
    if (first & 0x80) {
        if ((first & 0x70) || !rx->expected || rx->used >= rx->expected ||
            (first & 0x0f) != rx->sequence) goto invalid;
        rx->sequence = (rx->sequence + 1) & 0x0f;
    } else {
        rx->used = 0;
        rx->sequence = 0;
        switch (first & 0x60) {
        case 0x00: rx->expected = first & 0x1f; break;
        case 0x20:
            if (length < 2) goto invalid;
            header = 2;
            rx->expected = ((size_t)(first & 0x1f) << 8) | packet[1];
            break;
        case 0x40:
            if (length < 3 || (first & 0x1f)) goto invalid;
            header = 3;
            rx->expected = ((size_t)packet[1] << 8) | packet[2];
            break;
        default: goto invalid;
        }
        if (!rx->expected || rx->expected > sizeof(rx->data)) goto invalid;
    }
    if (length <= header || length - header > rx->expected - rx->used) goto invalid;
    memcpy(rx->data + rx->used, packet + header, length - header);
    rx->used += length - header;
    return rx->used == rx->expected ? 1 : 0;
invalid:
    rx->expected = rx->used = 0;
    return -1;
}

bool gopro_parse_status(const uint8_t *data, size_t length, gopro_status_t *status)
{
    if (!data || !status || length < 2 || data[0] != 0x13 || data[1] != 0) return false;
    gopro_status_t result = {0};
    unsigned found = 0;
    for (size_t i = 2; i < length;) {
        if (length - i < 2) return false;
        uint8_t id = data[i++];
        size_t size = data[i++];
        if (!size || size > length - i) return false;
        unsigned bit = 0;
        switch (id) {
        case 8: case 10: case 82:
            if (size != 1 || data[i] > 1) return false;
            if (id == 8) { result.busy = data[i]; bit = 1; }
            if (id == 10) { result.encoding = data[i]; bit = 2; }
            if (id == 82) { result.ready = data[i]; bit = 4; }
            break;
        case 70:
            if (size != 1 || data[i] > 100) return false;
            result.battery = data[i]; bit = 8;
            break;
        case 96:
            if (size != 4) return false;
            result.preset_group = ((uint32_t)data[i] << 24) | ((uint32_t)data[i+1] << 16) |
                ((uint32_t)data[i+2] << 8) | data[i+3];
            bit = 16;
            break;
        default: break;
        }
        if (found & bit) return false;
        found |= bit;
        i += size;
    }
    if (found != 31) return false;
    *status = result;
    return true;
}

bool gopro_advertisement(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length;) {
        size_t size = data[i++];
        if (!size || size > length - i) return false;
        if (data[i] == 0x02 || data[i] == 0x03) {
            if ((size - 1) % 2) return false;
            for (size_t j = 1; j + 1 < size; j += 2)
                if (data[i+j] == 0xa6 && data[i+j+1] == 0xfe) return true;
        }
        i += size;
    }
    return false;
}
