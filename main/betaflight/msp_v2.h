#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MSP_V2_SET_TEXT 0x3007
#define MSP_API_VERSION 1
#define MSP_FC_VARIANT 2
#define MSP_STATUS 101
#define MSP_BOXIDS 119
#define MSP_V2_MAX_PAYLOAD 256

typedef struct {
    uint16_t command;
    uint16_t length;
    bool error;
    uint8_t payload[MSP_V2_MAX_PAYLOAD];
} msp_v2_reply_t;

typedef struct {
    uint8_t bytes[MSP_V2_MAX_PAYLOAD + 9];
    size_t used;
} msp_v2_parser_t;

void msp_v2_parser_reset(msp_v2_parser_t *parser);
bool msp_v2_parser_feed(msp_v2_parser_t *parser, uint8_t byte, msp_v2_reply_t *reply);

uint8_t msp_v2_crc8_dvb_s2(uint8_t crc, uint8_t value);
size_t msp_v2_build_request(uint16_t command,
                            const uint8_t *payload,
                            size_t payload_length,
                            uint8_t *output,
                            size_t output_capacity);
