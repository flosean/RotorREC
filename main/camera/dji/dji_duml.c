#include "dji_duml.h"
#include <string.h>

static uint8_t duml_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x77;
    while (length-- > 0) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (uint8_t)((crc >> 1) ^ 0x8c) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

static uint16_t duml_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x3692;
    while (length-- > 0) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

size_t duml_build(uint8_t *output, size_t capacity,
                         uint8_t sender, uint8_t receiver, uint16_t sequence,
                         uint8_t flags, uint8_t command_set, uint8_t command_id,
                         const uint8_t *payload, size_t payload_length)
{
    size_t total_length = 13 + payload_length;
    if (output == NULL || payload_length > 0x3ff - 13 || total_length > capacity || total_length > 0x3ff ||
        (payload_length > 0 && payload == NULL)) {
        return 0;
    }

    output[0] = 0x55;
    output[1] = total_length & 0xff;
    output[2] = 0x04 | ((total_length >> 8) & 0x03);
    output[3] = duml_crc8(output, 3);
    output[4] = sender;
    output[5] = receiver;
    output[6] = sequence >> 8;
    output[7] = sequence & 0xff;
    output[8] = flags;
    output[9] = command_set;
    output[10] = command_id;
    if (payload_length > 0) {
        memcpy(output + 11, payload, payload_length);
    }
    uint16_t crc = duml_crc16(output, total_length - 2);
    output[total_length - 2] = crc & 0xff;
    output[total_length - 1] = crc >> 8;
    return total_length;
}

bool duml_parse(const uint8_t *data, size_t length, duml_frame_t *frame)
{
    if (data == NULL || frame == NULL || length < 13 || data[0] != 0x55) {
        return false;
    }
    size_t frame_length = data[1] | ((size_t)(data[2] & 0x03) << 8);
    if (frame_length < 13 || frame_length != length || (data[2] >> 2) != 1) {
        return false;
    }
    uint16_t received_crc = data[frame_length - 2] | ((uint16_t)data[frame_length - 1] << 8);
    if (data[3] != duml_crc8(data, 3) || received_crc != duml_crc16(data, frame_length - 2)) {
        return false;
    }

    *frame = (duml_frame_t) {
        .sequence = ((uint16_t)data[6] << 8) | data[7],
        .flags = data[8],
        .command_set = data[9],
        .command_id = data[10],
        .payload = data + 11,
        .payload_length = frame_length - 13,
    };
    return true;
}

void duml_rx_feed(duml_rx_t *rx, const uint8_t *data, size_t length,
                  duml_frame_callback_t callback)
{
    if (!rx || !data || !callback) return;
    while (length--) {
        rx->data[rx->used++] = *data++;
        while (rx->used) {
            size_t expected = rx->used >= 3
                ? rx->data[1] | ((size_t)(rx->data[2] & 3) << 8) : 0;
            bool invalid = rx->data[0] != 0x55;
            if (!invalid && rx->used < 4) break;
            if (!invalid) invalid = (rx->data[2] >> 2) != 1 || expected < 13 ||
                rx->data[3] != duml_crc8(rx->data, 3);
            if (!invalid && rx->used < expected) break;
            duml_frame_t frame;
            if (!invalid && duml_parse(rx->data, expected, &frame)) {
                callback(rx->data, expected);
                rx->used -= expected;
                memmove(rx->data, rx->data + expected, rx->used);
            } else {
                --rx->used;
                memmove(rx->data, rx->data + 1, rx->used);
            }
        }
    }
}
