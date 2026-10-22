#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Legacy 0x55 framing only. R SDK 0xAA and model-specific payloads stay separate.
 * Sequence is an opaque two-byte token; preserve the Action 2 capture convention. */
typedef struct {
    uint16_t sequence;
    uint8_t flags;
    uint8_t command_set;
    uint8_t command_id;
    const uint8_t *payload;
    size_t payload_length;
} duml_frame_t;

size_t duml_build(uint8_t *output, size_t capacity,
                  uint8_t sender, uint8_t receiver, uint16_t sequence,
                  uint8_t flags, uint8_t command_set, uint8_t command_id,
                  const uint8_t *payload, size_t payload_length);
bool duml_parse(const uint8_t *data, size_t length, duml_frame_t *frame);

/* BLE notifications may split or concatenate frames. Bounded by DUML's 10-bit length. */
typedef struct { uint8_t data[1023]; size_t used; } duml_rx_t;
typedef void (*duml_frame_callback_t)(const uint8_t *data, size_t length);
void duml_rx_feed(duml_rx_t *rx, const uint8_t *data, size_t length,
                  duml_frame_callback_t callback);
