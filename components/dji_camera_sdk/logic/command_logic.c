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
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "ble.h"
#include "data.h"
#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "status_logic.h"
#include "dji_protocol_parser.h"
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_COMMAND"

uint16_t s_current_seq = 0;

uint16_t generate_seq(void) {
    return s_current_seq += 1;
}

/**
 * @brief Send raw bytes directly without protocol frame creation
 *        Send raw bytes directly without constructing a protocol frame
 *
 * @param raw_data_string String containing raw bytes in various formats
 *                        String containing raw bytes in one of the supported formats
 * @param timeout_ms Timeout for waiting result (in milliseconds)
 *                   Result timeout in milliseconds
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                   ESP_OK on success; an error code on failure
 */
esp_err_t command_logic_send_raw_bytes(const char *raw_data_string, int timeout_ms) {
    if (connect_logic_get_state() <= BLE_INIT_COMPLETE) {
        ESP_LOGE(TAG, "BLE not connected");
        return ESP_ERR_INVALID_STATE;
    }

    return data_send_raw_bytes(raw_data_string, timeout_ms);
}

/**
 * @brief General function for constructing data frames and sending commands
 *        Generic helper to construct a frame and send a command
 *
 * @param cmd_set Command set, used to specify command category
 *                Command set identifying the command category
 * @param cmd_id Command ID, used to identify specific command
 *               Command ID identifying the operation
 * @param cmd_type Command type, indicates features like response requirement
 *                 Command type specifying acknowledgement requirements and other flags
 * @param structure Data structure pointer, contains input data for command frame
 *                 Pointer to the input structure used to construct the command frame
 * @param seq Sequence number, used to match request and response
 *            Sequence number used to match requests and responses
 * @param timeout_ms Timeout for waiting result (in milliseconds)
 *                   Result timeout in milliseconds
 * 
 * Note: The caller needs to free the dynamically allocated memory after using the returned structure.
 * The caller must free the dynamically allocated result after use
 * 
 * @return CommandResult Returns parsed structure pointer and data length on success, NULL pointer and length 0 on failure
 *                       Parsed structure and length on success; NULL and zero length on failure
 */
CommandResult send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *input_raw_data, uint16_t seq, int timeout_ms) { 
    CommandResult result = { NULL, 0 };

    if(connect_logic_get_state() <= BLE_INIT_COMPLETE){
        ESP_LOGE(TAG, "BLE not connected");
        return result;
    }

    esp_err_t ret;

    // Create protocol frame
    // Create the protocol frame
    size_t frame_length = 0;
    uint8_t *protocol_frame = protocol_create_frame(cmd_set, cmd_id, cmd_type, input_raw_data, seq, &frame_length);
    if (protocol_frame == NULL) {
        ESP_LOGE(TAG, "Failed to create protocol frame");
        return result;
    }

    ESP_LOGI(TAG, "Protocol frame created successfully, length: %zu", frame_length);

    // Print ByteArray format for debugging
    // Print in ByteArray format for debugging
    printf("\033[96m");  // Select cyan output
    printf("TX: [");
    for (size_t i = 0; i < frame_length; i++) {
        printf("%02X", protocol_frame[i]);
        if (i < frame_length - 1) {
            printf(", ");
        }
    }
    printf("] (%zu bytes)\n", frame_length);
    printf("\033[0m");
    printf("\033[0;32m");

    void *structure_data = NULL;
    size_t structure_data_length = 0;

    switch (cmd_type) {
        case CMD_NO_RESPONSE:
        case ACK_NO_RESPONSE:
            ret = data_write_without_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (no response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent without response.");
            break;

        case CMD_RESPONSE_OR_NOT:
        case ACK_RESPONSE_OR_NOT:
            ret = data_write_with_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (with response), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for response...");
            
            ret = data_wait_for_result_by_seq(seq, timeout_ms, &structure_data, &structure_data_length);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "No result received, but continuing (seq=0x%04X)", seq);
            }

            break;

        case CMD_WAIT_RESULT:
        case ACK_WAIT_RESULT:
            ret = data_write_with_response(seq, protocol_frame, frame_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send data frame (wait result), error: %s", esp_err_to_name(ret));
                free(protocol_frame);
                return result;
            }
            ESP_LOGI(TAG, "Data frame sent, waiting for result...");

            ret = data_wait_for_result_by_seq(seq, timeout_ms, &structure_data, &structure_data_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get parse result for seq=0x%04X, error: 0x%x", seq, ret);
                free(protocol_frame);
                return result;
            }

            if (structure_data == NULL) {
                ESP_LOGE(TAG, "Parse result is NULL for seq=0x%04X", seq);
                free(protocol_frame);
                return result;
            }

            break;

        default:
            ESP_LOGE(TAG, "Invalid cmd_type: %d", cmd_type);
            free(protocol_frame);
            return result;
    }

    free(protocol_frame);
    ESP_LOGI(TAG, "Command executed successfully");

    result.structure = structure_data;
    result.length = structure_data_length;

    return result;
}

/**
 * @brief Switch camera mode
 *        Switch camera mode
 *
 * @param mode Camera mode
 *             Camera mode
 * 
 * @return camera_mode_switch_response_frame_t* Returns parsed structure pointer, NULL on error
 *                                              Parsed structure pointer, or NULL on error
 */
camera_mode_switch_response_frame_t* command_logic_switch_camera_mode(camera_mode_t mode) {
    ESP_LOGI(TAG, "%s: Switching camera mode to: %d", __FUNCTION__, mode);
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    camera_mode_switch_command_frame_t command_frame = {
        .device_id = 0xFF330000,
        .mode = mode,
        .reserved = {0x01, 0x47, 0x39, 0x36}  // Reserved field
                                              // Reserved field
    };

    ESP_LOGI(TAG, "Constructed command frame: device_id=0x%08X, mode=%d", (unsigned int)command_frame.device_id, command_frame.mode);

    CommandResult result = send_command(
        0x1D,
        0x04,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    camera_mode_switch_response_frame_t *response = (camera_mode_switch_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Received response: ret_code=%d", response->ret_code);
    return response;
}

/**
 * @brief Query device version
 *        Query the device version
 *
 * This function sends a query command to get device version information.
 * Send a query to retrieve the device version information
 * 
 * The returned version information includes acknowledgment result (`ack_result`), 
 * product ID (`product_id`) and SDK version (`sdk_version`).
 * The response includes ack_result, product_id, and sdk_version
 *
 * @return version_query_response_frame_t* Returns parsed version info structure, NULL on error
 *                                         Parsed version information, or NULL on error
 */
version_query_response_frame_t* command_logic_get_version(void) {
    ESP_LOGI(TAG, "%s: Querying device version", __FUNCTION__);
    
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    CommandResult result = send_command(
        0x00,
        0x00,
        CMD_WAIT_RESULT,
        NULL,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    version_query_response_frame_t *response = (version_query_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Version Query Response: ack_result=%u, product_id=%s, sdk_version=%.*s",
             response->ack_result, response->product_id, 
             (int)(result.length - (sizeof(uint16_t) + sizeof(response->product_id))),
             response->sdk_version);

    return response;
}

/**
 * @brief Start recording
 *        Start recording
 *
 * @return record_control_response_frame_t* Returns parsed response structure pointer, NULL on error
 *                                          Parsed acknowledgement structure, or NULL on error
 */
record_control_response_frame_t* command_logic_start_record(void) {
    ESP_LOGI(TAG, "%s: Starting recording", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    record_control_command_frame_t command_frame = {
        .device_id = 0x33FF0000,
        .record_ctrl = 0x00,
        .reserved = {0x00, 0x00, 0x00, 0x00}
    };

    CommandResult result = send_command(
        0x1D,
        0x03,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    record_control_response_frame_t *response = (record_control_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Start Record Response: ret_code=%d", response->ret_code);

    return response;
}

/**
 * @brief Stop recording
 *        Stop recording
 *
 * @return record_control_response_frame_t* Returns parsed response structure pointer, NULL on error
 *                                          Parsed acknowledgement structure, or NULL on error
 */
record_control_response_frame_t* command_logic_stop_record(void) {
    ESP_LOGI(TAG, "%s: Stopping recording", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    record_control_command_frame_t command_frame = {
        .device_id = 0x33FF0000,
        .record_ctrl = 0x01,
        .reserved = {0x00, 0x00, 0x00, 0x00}
    };

    CommandResult result = send_command(
        0x1D,
        0x03,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    record_control_response_frame_t *response = (record_control_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Stop Record Response: ret_code=%d", response->ret_code);

    return response;
}

/**
 * @brief Push GPS data
 *        Push GPS data
 *
 * @param gps_data Pointer to structure containing GPS data
 *                 Pointer to the structure containing GPS data
 * 
 * @return gps_data_push_response_frame* Returns parsed response structure pointer, NULL on error
 *                                       Parsed acknowledgement structure, or NULL on error
 */
gps_data_push_response_frame* command_logic_push_gps_data(const gps_data_push_command_frame *gps_data) {
    ESP_LOGI(TAG, "Pushing GPS data");

    // Check connection status
    // Check connection state
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    if (gps_data == NULL) {
        ESP_LOGE(TAG, "Invalid input: gps_data is NULL");
        return NULL;
    }

    uint16_t seq = generate_seq();

    // Send command and receive response
    // Send the command and receive its acknowledgement
    CommandResult result = send_command(
        0x00,
        0x17,
        CMD_NO_RESPONSE,
        gps_data,
        seq,
        5000
    );

    // Return response structure pointer
    // Return the acknowledgement structure
    return (gps_data_push_response_frame *)result.structure;
}

/**
 * @brief Quick switch mode key report
 *        Report a quick-switch mode button event
 *
 * @return key_report_response_frame_t* Returns parsed response structure pointer, NULL on error
 *                                      Parsed acknowledgement structure, or NULL on error
 */
key_report_response_frame_t* command_logic_key_report_qs(void) {
    ESP_LOGI(TAG, "%s: Reporting key press for mode switch", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    key_report_command_frame_t command_frame = {
        .key_code = 0x02,          // QS key code for mode switch
                                   // QS button code for mode switching
        .mode = 0x01,              // Fixed as 0x01
                                   // Fixed value: 0x01
        .key_value = 0x00,         // Fixed as 0x00, short press event
                                   // Fixed value: 0x00, short-press event
    };

    CommandResult result = send_command(
        0x00,
        0x11,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    key_report_response_frame_t *response = (key_report_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Key Report Response: ret_code=%d", response->ret_code);

    return response;
}


key_report_response_frame_t* command_logic_key_report_snapshot(void) {
    ESP_LOGI(TAG, "%s: Reporting key press for snapshot", __FUNCTION__);

    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();

    key_report_command_frame_t command_frame = {
        .key_code = 0x03,          // Snapshot key code
                                   // Photo button code
        .mode = 0x01,              // Fixed as 0x01
                                   // Fixed value: 0x01
        .key_value = 0x00,         // Fixed as 0x00, short press event
                                   // Fixed value: 0x00, short-press event
    };

    CommandResult result = send_command(
        0x00,
        0x11,
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );
    
    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    key_report_response_frame_t *response = (key_report_response_frame_t *)result.structure;

    ESP_LOGI(TAG, "Key Report Response: ret_code=%d", response->ret_code);

    return response;
}