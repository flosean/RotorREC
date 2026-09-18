#pragma once
#include <stdint.h>

/* This user's C3-Zero LED swaps red/green when sent GRB (2026-09-12).
   Use RGB for this board; do not assume every WS2812-labelled batch is RGB.
   Shared with host tests so logical-color tests cannot hide a wire-order bug. */
static inline uint32_t status_led_wire_color(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}
