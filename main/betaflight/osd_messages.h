#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BETAFLIGHT_OSD_CUSTOM_MESSAGE_COUNT 4
#define BETAFLIGHT_OSD_CUSTOM_MESSAGE_LENGTH 16
#define BETAFLIGHT_OSD_MAX_FRAME_LENGTH 27

size_t betaflight_osd_build_custom_message(uint8_t line,
                                           const char *text,
                                           uint8_t *output,
                                           size_t output_capacity);
bool betaflight_protocol_self_check(void);
