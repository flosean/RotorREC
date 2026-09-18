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

#include <string.h>
#include <stdio.h>
#include "esp_log.h"

#include "dji_protocol_data_processor.h"

#define TAG "DJI_PROTOCOL_DATA_PROCESSOR"

/**
 * @brief Find data descriptor by command set and command ID
 *        Find the data descriptor for a command set and command ID
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @return Return pointer to found data descriptor, NULL if not found
 *         Matching data descriptor, or NULL if not found
 */
const data_descriptor_t *find_data_descriptor(uint8_t cmd_set, uint8_t cmd_id) {
    for (size_t i = 0; i < DATA_DESCRIPTORS_COUNT; ++i) {
        if (data_descriptors[i].cmd_set == cmd_set && data_descriptors[i].cmd_id == cmd_id) {
            return &data_descriptors[i];
        }
    }
    return NULL;
}

/**
 * @brief Parse data according to structure
 *        Parse data into a structure
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @param cmd_type Command type
 *                 Command type
 * @param data Data to be parsed
 *             Data to parse
 * @param data_length Data length
 *                    Data length
 * @param structure_out Output structure pointer
 *                      Output structure pointer
 * @return Return 0 on success, -1 or -2 on failure
 *         Return 0 on success, or -1 or -2 on failure
 */
int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, void *structure_out) {
    ESP_LOGI(TAG, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X", cmd_set, cmd_id, cmd_type);

    // Find corresponding descriptor
    // Find the matching command descriptor
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);

    // Check if parser function exists
    // Check that a parser is available
    if (descriptor->parser == NULL) {
        ESP_LOGW(TAG, "Parser function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return -2;
    }

    return descriptor->parser(data, data_length, structure_out, cmd_type);
}

/**
 * @brief Create data according to structure
 *        Create data from a structure
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @param cmd_type Command type
 *                 Command type
 * @param structure Input structure pointer
 *                  Input structure pointer
 * @param data_length Output data length
 *                    Output data length
 * @return Return pointer to created data buffer, NULL on failure
 *         Created data buffer, or NULL on failure
 */
uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, size_t *data_length) {
    // Find corresponding descriptor
    // Find the matching command descriptor
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        ESP_LOGW(TAG, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return NULL;
    }

    // Check if creator function exists
    // Check that a creator is available
    if (descriptor->creator == NULL) {
        ESP_LOGW(TAG, "Creator function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X", cmd_set, cmd_id);
        return NULL;
    }

    return descriptor->creator(structure, data_length, cmd_type);
}
