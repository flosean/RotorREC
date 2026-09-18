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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ble.h"
#include "data.h"
#include "enums_logic.h"
#include "connect_logic.h"
#include "command_logic.h"
#include "status_logic.h"
#include "dji_protocol_data_structures.h"

#define TAG "LOGIC_CONNECT"

static connect_state_t connect_state = BLE_NOT_INIT;

/**
 * @brief Get current connection state
 *        Get the current connection state
 * 
 * @return connect_state_t Returns current connection state
 *                        Return the current connection state
 */
connect_state_t connect_logic_get_state(void) {
    return connect_state;
}

/**
 * @brief Handle camera disconnection (callback function)
 *        Camera disconnection callback
 * 
 * Perform operations according to current connection state and reset connection state to BLE initialization complete (BLE_INIT_COMPLETE).
 * Handle the current state and reset it to BLE_INIT_COMPLETE
 */
void receive_camera_disconnect_handler() {
    switch (connect_state) {
        case BLE_SEARCHING:
            break;
        case BLE_INIT_COMPLETE:
            ESP_LOGI(TAG, "Already in DISCONNECTED state.");
            break;
        case BLE_DISCONNECTING: {
            ESP_LOGI(TAG, "Normal disconnection process.");
            // Normal disconnection also needs to reset state
            // Reset state after an intentional disconnect as well
            connect_state = BLE_INIT_COMPLETE;
            camera_status_initialized = false;
            ESP_LOGI(TAG, "Current state: DISCONNECTED.");
            break;
        }
        case BLE_CONNECTED:
        case PROTOCOL_CONNECTED:
        default: {
            ESP_LOGW(TAG, "Unexpected disconnection from state: %d, attempting reconnection...", connect_state);
            
            // Try to reconnect once
            // Attempt reconnection once
            bool reconnected = false;
            ESP_LOGI(TAG, "Reconnection attempt...");
            if (connect_logic_ble_connect(true) == ESP_OK) {
                // Wait for reconnection result
                // Wait for the reconnection result
                for (int j = 0; j < 300; j++) { // Wait for 30 seconds
                                                // Wait for 30 seconds
                    if (s_ble_profile.connection_status.is_connected) {
                        ESP_LOGI(TAG, "Reconnection successful");
                        reconnected = true;
                        return;  // Return immediately if reconnection succeeds
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }

            if (!reconnected) {
                ESP_LOGE(TAG, "Reconnection failed after 1 attempts");
                // Reconnection failed, execute disconnection logic
                // Run disconnection handling if reconnection fails
                connect_state = BLE_INIT_COMPLETE;
                camera_status_initialized = false;
                ble_disconnect();
                ESP_LOGI(TAG, "Current state: DISCONNECTED.");
            }
            break;
        }
    }
}

/**
 * @brief Initialize BLE connection
 *        Initialize the BLE connection
 * 
 * Initialize BLE and set state to BLE initialization complete (BLE_INIT_COMPLETE).
 * Initialize BLE and set the state to BLE_INIT_COMPLETE
 * 
 * @return int Returns 0 on success, -1 on failure
 *             Return 0 on success, or -1 on failure
 */
int connect_logic_ble_init() {
    esp_err_t ret;

    /* 1. Initialize BLE (specify target device name to search and connect)
     * 1. Initialize BLE with the target device name */
    ret = ble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE, error: %s", esp_err_to_name(ret));
        return -1;
    }

    connect_state = BLE_INIT_COMPLETE;
    ESP_LOGI(TAG, "BLE init successfully");
    return 0;
}

/**
 * @brief Connect to BLE device
 *        Connect to a BLE device
 * 
 * Execute the following steps: set callbacks, start scanning and attempt connection, wait for connection completion and characteristic handle discovery.
 * Set callbacks, scan and connect, then wait for connection and characteristic discovery
 * 
 * If connection fails, returns error and resets connection state.
 * Return an error and reset connection state if the connection fails
 * 
 * @return int Returns 0 on success, -1 on failure
 *             Return 0 on success, or -1 on failure
 */
int connect_logic_ble_connect(bool is_reconnecting) {
    connect_state = BLE_SEARCHING;

    esp_err_t ret;

    /* 1. Set a global Notify callback for receiving remote data and protocol parsing */
    /* Set the global notification callback for incoming data and protocol parsing */
    ble_set_notify_callback(receive_camera_notify_handler);
    ble_set_state_callback(receive_camera_disconnect_handler);

    /* 2. Start scanning and attempt connection */
    /* Start scanning and attempt a connection */
    ble_set_reconnecting(is_reconnecting);
    ret = ble_start_scanning_and_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scanning and connect, error: 0x%x", ret);
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    /* 3. Wait up to 30 seconds to ensure BLE connection success */
    /* Wait up to 30 seconds for the BLE connection */
    ESP_LOGI(TAG, "Waiting up to 30s for BLE to connect...");
    bool connected = false;
    for (int i = 0; i < 300; i++) { // 300 * 100ms = 30s
        if (s_ble_profile.connection_status.is_connected) {
            ESP_LOGI(TAG, "BLE connected successfully");
            connected = true;
            break;
        }
        if (is_reconnecting && ble_connect_attempt_finished()) {
            ESP_LOGI(TAG, "Reconnect scan/open finished without connection; retry promptly");
            connect_state = BLE_INIT_COMPLETE;
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!connected) {
        ESP_LOGW(TAG, "BLE connection timed out");
        esp_err_t cancel_result = ble_cancel_pending_connect();
        if (cancel_result != ESP_OK) {
            ESP_LOGE(TAG, "Could not cancel timed-out BLE connection: %s",
                     esp_err_to_name(cancel_result));
        }
        connect_state = BLE_INIT_COMPLETE;
        return -1;
    }

    /* 4. Wait for characteristic handle discovery completion (up to 30 seconds) */
    /* Wait up to 30 seconds for characteristic handle discovery */
    ESP_LOGI(TAG, "Waiting up to 30s for characteristic handles discovery...");
    bool handles_found = false;
    for (int i = 0; i < 300; i++) { // 300 * 100ms = 30s
        if (!s_ble_profile.connection_status.is_connected) {
            ESP_LOGW(TAG, "Link lost during service discovery; retry immediately");
            connect_state = BLE_INIT_COMPLETE;
            return -1;
        }
        if (s_ble_profile.handle_discovery.notify_char_handle_found && 
            s_ble_profile.handle_discovery.write_char_handle_found) {
            ESP_LOGI(TAG, "Required characteristic handles found");
            handles_found = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!handles_found) {
        ESP_LOGW(TAG, "Characteristic handles not found within timeout");
        connect_state = BLE_INIT_COMPLETE;
        ble_disconnect();
        return -1;
    }

    /* 5. Register notification */
    /* Register notifications */
    ret = ble_register_notify(s_ble_profile.conn_id, s_ble_profile.notify_char_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register notify, error: %s", esp_err_to_name(ret));
        connect_state = BLE_INIT_COMPLETE;
        ble_disconnect();
        return -1;
    }

    // Update state to BLE connected
    // Set the state to BLE connected
    connect_state = BLE_CONNECTED;

    // Delay RGB light display
    // Delay the ambient light display
    if (!is_reconnecting) vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "BLE connect successfully");
    return 0;
}

/**
 * @brief Disconnect BLE connection
 *        Disconnect BLE
 * 
 * Attempt to disconnect from BLE device.
 * Attempt to disconnect the BLE device
 * 
 * @return int Returns 0 on success, -1 on failure
 *             Return 0 on success, or -1 on failure
 */
int connect_logic_ble_disconnect(void) {
    connect_state_t old_state = connect_state;
    connect_state = BLE_DISCONNECTING;
    
    ESP_LOGI(TAG, "Disconnecting camera...");

    // Call BLE layer's ble_disconnect function
    // Call ble_disconnect in the BLE layer
    esp_err_t ret = ble_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect camera, BLE error: %s", esp_err_to_name(ret));
        connect_state = old_state;
        return -1;
    }

    ESP_LOGI(TAG, "Camera disconnected successfully");
    return 0;
}

/**
 * @brief Protocol connection function
 *        Establish the protocol session
 * 
 * This function is responsible for establishing protocol connection, including the following steps:
 * Establish the protocol connection using these steps:
 * 
 * 1. Send connection request command to camera.
 *    Send a connection request to the camera
 * 2. Wait for camera's response and verify.
 *    Wait for and validate the camera response
 * 3. Send connection response according to camera's returned command.
 *    Send a connection acknowledgement based on the camera command
 * 4. Set connection state to protocol connected.
 *    Set the state to protocol connected
 * 
 * @param device_id Device ID
 *                  Device ID
 * @param mac_addr_len MAC address length
 *                     MAC address length
 * @param mac_addr Pointer to MAC address
 *                 Pointer to the MAC address
 * @param fw_version Firmware version
 *                   Firmware version
 * @param verify_mode Verification mode
 *                    Verification mode
 * @param verify_data Verification data
 *                    Verification data
 * @param camera_reserved Camera reserved field
 *                        Camera reserved field
 * 
 * @return int Returns 0 on success, -1 on failure
 *             Return 0 on success, or -1 on failure
 */
int connect_logic_protocol_connect(uint32_t device_id, uint8_t mac_addr_len, const int8_t *mac_addr,
                                   uint32_t fw_version, uint8_t verify_mode, uint16_t verify_data,
                                   uint8_t camera_reserved) {
    ESP_LOGI(TAG, "%s: Starting protocol connection", __FUNCTION__);
    uint16_t seq = generate_seq();

    // Construct connection request command frame
    // Construct the connection request frame
    connection_request_command_frame connection_request = {
        .device_id = device_id,
        .mac_addr_len = mac_addr_len,
        .fw_version = fw_version,
        .verify_mode = verify_mode,
        .verify_data = verify_data,
    };
    memcpy(connection_request.mac_addr, mac_addr, mac_addr_len);


    // STEP1: Send connection request command to camera
    // The camera sends a connection request command
    ESP_LOGI(TAG, "Sending connection request to camera...");
    CommandResult result = send_command(0x00, 0x19, CMD_WAIT_RESULT, &connection_request, seq, 1000);

    /**** Connection issue: camera may return either response frame or command frame ****/
    /****************** The camera may return either an acknowledgement frame or a command frame*/

    if (result.structure == NULL) {
        // If a command frame is sent, execute this block of code
        // Handle a command frame here

        // Directly call data_wait_for_result_by_cmd(0x00, 0x19, 30000, &received_seq, &parse_result, &parse_result_length);
        // Call data_wait_for_result_by_cmd(0x00, 0x19, 30000, &received_seq, &parse_result, &parse_result_length)
        
        // If != OK, it means no message was received, timeout occurred
        // A result other than OK means no message arrived before timeout
        
        // Otherwise, GOTO wait_for_camera_command label
        // Otherwise, jump to wait_for_camera_command
        void *parse_result = NULL;
        size_t parse_result_length = 0;
        uint16_t received_seq = 0;
        esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 1000, &received_seq, &parse_result, &parse_result_length);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Timeout or error waiting for camera connection command, GOTO Failed.");
            connect_logic_ble_disconnect();
            return -1;
        } else {
            // If data is received, skip parsing camera response and directly enter STEP3
            // If data arrives, skip response parsing and proceed to STEP3
            goto wait_for_camera_command;
        }
    }

    // STEP2: Parse the response returned from camera
    // Parse the camera response
    connection_request_response_frame *response = (connection_request_response_frame *)result.structure;
    if (response->ret_code != 0) {
        ESP_LOGE(TAG, "Connection handshake failed: unexpected response from camera, ret_code: %d", response->ret_code);
        free(response);
        connect_logic_ble_disconnect();
        return -1;
    }

    ESP_LOGI(TAG, "Handshake successful, waiting for the camera to actively send the connection command frame...");
    free(response);

    // STEP3: Wait for camera to send connection request
    // Wait for an unsolicited camera connection request
wait_for_camera_command:
    void *parse_result = NULL;
    size_t parse_result_length = 0;
    uint16_t received_seq = 0;
    esp_err_t ret = data_wait_for_result_by_cmd(0x00, 0x19, 30000, &received_seq, &parse_result, &parse_result_length);

    if (ret != ESP_OK || parse_result == NULL) {
        ESP_LOGE(TAG, "Timeout or error waiting for camera connection command");
        connect_logic_ble_disconnect();
        return -1;
    }

    // Parse the connection request command sent by camera
    // Parse the camera's connection request command
    connection_request_command_frame *camera_request = (connection_request_command_frame *)parse_result;

    if (camera_request->verify_mode != 2) {
        ESP_LOGE(TAG, "Unexpected verify_mode from camera: %d", camera_request->verify_mode);
        free(parse_result);
        connect_logic_ble_disconnect();
        return -1;
    }

    if (camera_request->verify_data == 0) {
        ESP_LOGI(TAG, "Camera approved the connection, sending response...");

        // Construct connection response frame
        // Construct the connection acknowledgement frame
        connection_request_response_frame connection_response = {
            .device_id = device_id,
            .ret_code = 0,
        };
        memset(connection_response.reserved, 0, sizeof(connection_response.reserved));
        connection_response.reserved[0] = camera_reserved;

        ESP_LOGI(TAG, "Constructed connection response, sending...");

        // STEP4: Send connection response frame
        // Send the connection acknowledgement frame
        send_command(0x00, 0x19, ACK_NO_RESPONSE, &connection_response, received_seq, 5000);

        // Set connection state to protocol connected
        // Set the state to protocol connected
        connect_state = PROTOCOL_CONNECTED;

        ESP_LOGI(TAG, "Connection successfully established with camera.");
        free(parse_result);
        return 0;
    } else {
        ESP_LOGW(TAG, "Camera rejected the connection, closing Bluetooth link...");
        free(parse_result);
        connect_logic_ble_disconnect();
        return -1;
    }
}

int connect_logic_ble_wakeup(void) {
    ESP_LOGI(TAG, "Attempting to wake up camera via BLE advertising");

    esp_err_t ret = ble_start_advertising();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE advertising: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "BLE advertising started, attempting to wake up camera");
    return 0;
}
