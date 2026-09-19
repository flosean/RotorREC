#include "protocol.h"
#include <string.h>

uint32_t rr_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
void rr_put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8 * i));
}
uint32_t rr_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
size_t rr_encode(const rr_packet_t *p, uint8_t *out)
{
    if (p->length > RR_PAYLOAD_MAX) return 0;
    memcpy(out, "RREC", 4);
    out[4] = 1; out[5] = p->command;
    rr_put32(out + 6, p->session); rr_put32(out + 10, p->sequence);
    out[14] = (uint8_t)p->length; out[15] = (uint8_t)(p->length >> 8);
    memcpy(out + 16, p->payload, p->length);
    rr_put32(out + 16 + p->length, rr_crc32(out, 16 + p->length));
    return 20 + p->length;
}
bool rr_feed(rr_parser_t *s, uint8_t byte, rr_packet_t *p)
{
    s->data[s->used++] = byte;
    while (s->used) {
        size_t prefix = s->used < 4 ? s->used : 4;
        if (memcmp(s->data, "RREC", prefix) != 0 || (s->used >= 5 && s->data[4] != 1)) goto discard;
        if (s->used < 16) return false;
        size_t length = s->data[14] | (size_t)s->data[15] << 8;
        if (length > RR_PAYLOAD_MAX) goto discard;
        if (s->used < length + 20) return false;
        if (rr_u32(s->data + 16 + length) != rr_crc32(s->data, 16 + length)) goto discard;
        p->command = s->data[5]; p->session = rr_u32(s->data + 6);
        p->sequence = rr_u32(s->data + 10); p->length = (uint16_t)length;
        memcpy(p->payload, s->data + 16, length);
        s->used -= length + 20;
        memmove(s->data, s->data + length + 20, s->used);
        return true;
discard:
        --s->used;
        memmove(s->data, s->data + 1, s->used);
    }
    return false;
}
