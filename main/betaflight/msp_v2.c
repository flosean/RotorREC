#include "msp_v2.h"

#include <string.h>

void msp_v2_parser_reset(msp_v2_parser_t *parser)
{
    parser->used = 0;
}

bool msp_v2_parser_feed(msp_v2_parser_t *parser, uint8_t byte, msp_v2_reply_t *reply)
{
    parser->bytes[parser->used++] = byte;
    while (parser->used != 0) {
        uint8_t *b = parser->bytes;
        bool bad = b[0] != '$' || (parser->used >= 2 && b[1] != 'X') ||
            (parser->used >= 3 && b[2] != '>' && b[2] != '!') ||
            (parser->used >= 4 && b[3] != 0);
        size_t length = parser->used >= 8 ? b[6] | ((size_t)b[7] << 8) : 0;
        bad |= length > MSP_V2_MAX_PAYLOAD;
        if (!bad && parser->used < 8 + length + 1) {
            return false;
        }
        if (!bad) {
            uint8_t crc = 0;
            for (size_t i = 3; i < 8 + length; ++i) {
                crc = msp_v2_crc8_dvb_s2(crc, b[i]);
            }
            if (crc == b[8 + length]) {
                reply->command = b[4] | ((uint16_t)b[5] << 8);
                reply->length = (uint16_t)length;
                reply->error = b[2] == '!';
                memcpy(reply->payload, b + 8, length);
                parser->used -= length + 9;
                memmove(b, b + length + 9, parser->used);
                return true;
            }
        }
        /* Slide past noise, corrupt CRCs and oversized headers without allocating. */
        --parser->used;
        memmove(b, b + 1, parser->used);
    }
    return false;
}

uint8_t msp_v2_crc8_dvb_s2(uint8_t crc, uint8_t value)
{
    crc ^= value;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
    return crc;
}

size_t msp_v2_build_request(uint16_t command,
                            const uint8_t *payload,
                            size_t payload_length,
                            uint8_t *output,
                            size_t output_capacity)
{
    const size_t frame_length = 9 + payload_length;
    if (output == NULL || payload_length > UINT16_MAX || output_capacity < frame_length ||
        (payload_length > 0 && payload == NULL)) {
        return 0;
    }

    output[0] = '$';
    output[1] = 'X';
    output[2] = '<';
    output[3] = 0;
    output[4] = (uint8_t)(command & 0xFF);
    output[5] = (uint8_t)(command >> 8);
    output[6] = (uint8_t)(payload_length & 0xFF);
    output[7] = (uint8_t)(payload_length >> 8);
    if (payload_length > 0) {
        memcpy(output + 8, payload, payload_length);
    }

    uint8_t crc = 0;
    for (size_t i = 3; i < 8 + payload_length; ++i) {
        crc = msp_v2_crc8_dvb_s2(crc, output[i]);
    }
    output[8 + payload_length] = crc;
    return frame_length;
}
