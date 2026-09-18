/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 SZ DJI Technology Co., Ltd.
 *  
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI’s authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 */

#ifndef DJI_PROTOCOL_PARSER_H
#define DJI_PROTOCOL_PARSER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Protocol frame parsing result structure
 *        Parsed protocol frame structure
 */
typedef struct {
    uint8_t sof;            // Start of frame (SOF)
                            // Start of frame (SOF)
    uint16_t version;       // Protocol version
                            // Version number
    uint16_t frame_length;  // Total frame length
                            // Frame length
    uint8_t cmd_type;       // Command type (CmdType)
                            // Command type (CmdType)
    uint8_t enc;            // Encryption flag (ENC)
                            // Encryption flag (ENC)
    uint8_t res[3];         // Reserved field (RES)
                            // Reserved field (RES)
    uint16_t seq;           // Sequence number (SEQ)
                            // Sequence number (SEQ)
    uint16_t crc16;         // CRC-16 checksum
                            // CRC-16 checksum value
    const uint8_t *data;    // Pointer to data segment (DATA)
                            // DATA segment pointer
    size_t data_length;     // Length of data segment
                            // DATA segment length
    uint32_t crc32;         // CRC-32 checksum
                            // CRC-32 checksum value
} protocol_frame_t;

int protocol_parse_notification(const uint8_t *frame_data, size_t frame_length, protocol_frame_t *frame_out);

void* protocol_parse_data(const uint8_t *data, size_t data_length, uint8_t cmd_type, size_t *data_length_without_cmd_out);

uint8_t* protocol_create_frame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, uint16_t seq, size_t *frame_length_out);

#endif