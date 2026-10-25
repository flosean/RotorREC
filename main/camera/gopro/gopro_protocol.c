#include "gopro_protocol.h"

#include <string.h>
#include <stdio.h>

static uint64_t big_endian(const uint8_t *data, size_t length)
{
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) value = (value << 8) | data[i];
    return value;
}

bool gopro_api_version_supported(const uint8_t *data, size_t length)
{
    /* 0x51 response: result, major-length/value, minor-length/value.
     * Match the official SDK's supported API 2.0, not arbitrary future 2.x. */
    return data && length == 6 && data[0] == GOPRO_CMD_API_VERSION && data[1] == 0 &&
        data[2] == 1 && data[3] == 2 && data[4] == 1 && data[5] == 0;
}

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
    if (!data || !status || length < 2 || data[0] != GOPRO_QUERY_STATUS || data[1] != 0) return false;
    gopro_status_t result = {0};
    unsigned found = 0;
    uint8_t seen[32] = {0};
    for (size_t i = 2; i < length;) {
        if (length - i < 2) return false;
        uint8_t id = data[i++];
        size_t size = data[i++];
        if (size > length - i || (seen[id / 8] & (1U << (id % 8)))) return false;
        seen[id / 8] |= (uint8_t)(1U << (id % 8));
        unsigned bit = 0;
        switch (id) {
        case GOPRO_STATUS_BUSY: case GOPRO_STATUS_ENCODING: case GOPRO_STATUS_READY:
            if (size != 1 || data[i] > 1) return false;
            if (id == GOPRO_STATUS_BUSY) { result.busy = data[i]; bit = 1; }
            if (id == GOPRO_STATUS_ENCODING) { result.encoding = data[i]; bit = 2; }
            if (id == GOPRO_STATUS_READY) { result.ready = data[i]; bit = 4; }
            break;
        case GOPRO_STATUS_BATTERY:
            if (size != 1 || data[i] > 100) return false;
            result.battery = data[i]; bit = 8;
            break;
        case GOPRO_STATUS_PRESET_GROUP:
            if (size != 4) return false;
            result.preset_group = ((uint32_t)data[i] << 24) | ((uint32_t)data[i+1] << 16) |
                ((uint32_t)data[i+2] << 8) | data[i+3];
            bit = 16;
            break;
        /* Optional values from the official Open GoPro API 2.0 status schema.
         * Missing fields stay invalid; unknown enums are never treated as OK. */
        case 13: case 35:
            if (size != 4) return false;
            if (id == 13) {
                result.telemetry.record_time = (uint32_t)big_endian(data + i, 4);
                result.telemetry.record_time_valid = true;
            } else {
                result.telemetry.remain_time = (uint32_t)big_endian(data + i, 4);
                result.telemetry.remain_time_valid = true;
            }
            break;
        case 54: {
            if (size != 8) return false;
            uint64_t mb = big_endian(data + i, 8) / 1024; /* API reports KiB. */
            result.telemetry.remain_capacity_valid = mb <= UINT32_MAX;
            result.telemetry.remain_capacity = (uint32_t)mb;
            break;
        }
        case 33:
            if (size != 1) return false;
            result.telemetry.storage = data[i];
            result.telemetry.storage_valid = data[i] <= 4 || data[i] == 8;
            break;
        case 6: case 85: case 111:
            if (size != 1 || data[i] > 1) return false;
            if (id == 6) {
                result.telemetry.temperature_valid = true;
                result.telemetry.temperature = data[i] ? 2 : 0;
            } else if (id == 85) {
                result.telemetry.cold_valid = true; result.telemetry.cold = data[i];
            } else {
                result.telemetry.write_speed_valid = true; result.telemetry.write_speed_error = data[i];
            }
            break;
        default: break;
        }
        if (found & bit) return false;
        found |= bit;
        i += size;
    }
    if (found != 31) return false;
    result.telemetry.valid = true;
    result.telemetry.recording = result.encoding;
    result.telemetry.battery = result.battery;
    result.telemetry.mode_valid = true;
    result.telemetry.video_mode = result.preset_group == GOPRO_PRESET_GROUP_VIDEO;
    result.telemetry.recording_valid = result.telemetry.video_mode;
    result.telemetry.record_time_valid &= result.telemetry.video_mode;
    result.telemetry.remain_time_valid &= result.telemetry.video_mode;
    snprintf(result.telemetry.mode, sizeof(result.telemetry.mode), "%s",
        result.telemetry.video_mode ? "VIDEO" : "NON-VIDEO");
    *status = result;
    return true;
}

static const char *resolution(uint8_t value)
{
    switch (value) {
    case 1: return "4K"; case 4: return "2.7K"; case 6: case 111: return "2.7K4:3";
    case 7: return "1440"; case 9: return "1080"; case 12: return "720";
    case 18: case 112: return "4K4:3"; case 21: return "5.6K";
    case 24: return "5K"; case 25: return "5K4:3";
    case 26: case 107: return "5.3K8:7"; case 27: case 113: return "5.3K4:3";
    case 28: case 108: return "4K8:7"; case 31: return "8K";
    case 35: return "5.3K21:9"; case 36: return "4K21:9";
    case 37: return "4K1:1"; case 38: return "900"; case 39: return "4KSPH";
    case 100: return "5.3K"; case 109: return "4K9:16"; case 110: return "10809:16";
    default: return NULL;
    }
}

bool gopro_parse_format(const uint8_t *data, size_t length, char parameters[21])
{
    if (!data || !parameters || length < 2 || data[0] != GOPRO_QUERY_SETTINGS || data[1]) return false;
    const char *res = NULL;
    unsigned fps = 0, found = 0;
    for (size_t i = 2; i < length;) {
        if (length - i < 2) return false;
        uint8_t id = data[i++], size = data[i++];
        if (size > length - i) return false;
        if (id == 2 || id == 3) {
            unsigned bit = id == 2 ? 1 : 2;
            if (size != 1 || (found & bit)) return false;
            found |= bit;
            if (id == 2) res = resolution(data[i]);
            else switch (data[i]) {
            case 0: fps = 240; break; case 1: fps = 120; break; case 2: fps = 100; break;
            case 3: fps = 90; break; case 5: fps = 60; break; case 6: fps = 50; break;
            case 8: fps = 30; break; case 9: fps = 25; break; case 10: fps = 24; break;
            case 13: fps = 200; break; case 15: fps = 400; break; case 16: fps = 360; break;
            case 17: fps = 300; break; default: break;
            }
        }
        i += size;
    }
    if (found != 3 || !res || !fps) return false;
    snprintf(parameters, 21, "%s %u", res, fps);
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
