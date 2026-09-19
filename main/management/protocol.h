#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RR_PAYLOAD_MAX 1040
#define RR_FRAME_MAX (RR_PAYLOAD_MAX + 20)
enum { RR_HELLO = 1, RR_INFO, RR_SET_CAMERA, RR_PAIR, RR_BEGIN, RR_DATA,
       RR_END, RR_ABORT, RR_REBOOT, RR_EXIT };
typedef struct {
    uint8_t command;
    uint32_t session, sequence;
    uint16_t length;
    uint8_t payload[RR_PAYLOAD_MAX];
} rr_packet_t;
typedef struct { uint8_t data[RR_FRAME_MAX]; size_t used; } rr_parser_t;
uint32_t rr_u32(const uint8_t *p);
void rr_put32(uint8_t *p, uint32_t n);
uint32_t rr_crc32(const uint8_t *data, size_t length);
size_t rr_encode(const rr_packet_t *packet, uint8_t *out);
bool rr_feed(rr_parser_t *parser, uint8_t byte, rr_packet_t *packet);
