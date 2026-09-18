#include "osd_messages.h"

#include <string.h>

#include "msp_v2.h"

#define MSP2_TEXT_CUSTOM_MESSAGE_0 7

size_t betaflight_osd_build_custom_message(uint8_t line,
                                           const char *text,
                                           uint8_t *output,
                                           size_t output_capacity)
{
    if (line >= BETAFLIGHT_OSD_CUSTOM_MESSAGE_COUNT || text == NULL) {
        return 0;
    }
    size_t text_length = strnlen(text, BETAFLIGHT_OSD_CUSTOM_MESSAGE_LENGTH);
    uint8_t payload[2 + BETAFLIGHT_OSD_CUSTOM_MESSAGE_LENGTH];
    payload[0] = MSP2_TEXT_CUSTOM_MESSAGE_0 + line;
    payload[1] = (uint8_t)text_length;
    memcpy(payload + 2, text, text_length);
    return msp_v2_build_request(MSP_V2_SET_TEXT, payload, 2 + text_length,
                                output, output_capacity);
}

bool betaflight_protocol_self_check(void)
{
    static const uint8_t expected[] = {
        '$', 'X', '<', 0x00, 0x07, 0x30, 0x05, 0x00,
        0x07, 0x03, 'R', 'E', 'C', 0xDF,
    };
    uint8_t frame[BETAFLIGHT_OSD_MAX_FRAME_LENGTH] = {0};
    size_t length = betaflight_osd_build_custom_message(0, "REC", frame, sizeof(frame));
    if (length != sizeof(expected)) {
        return false;
    }
    return memcmp(frame, expected, sizeof(expected)) == 0;
}
