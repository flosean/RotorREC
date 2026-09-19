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
#include "ble.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_timer.h"

#define TAG "BLE"

/* Target device name */
/* Target device name */
static char s_remote_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = {0};

/* Flags indicating whether a connection has been initiated and whether the target service has been found, for demonstration only */
/* Demo flags for connection attempts and service discovery */
static volatile bool s_connecting = false;

/* Globally saved Notify callback */
/* Global notification callback */
static ble_notify_callback_t s_notify_cb = NULL;

/* Set logic layer disconnection state callback */
/* Logic-layer disconnection callback */
static connect_logic_state_callback_t s_state_cb = NULL;

/* Attempt to connect when the target device is scanned */
/* Connect when the target device is found */
#define MIN_RSSI_THRESHOLD -80          // Set minimum signal strength threshold, adjust as needed
static esp_bd_addr_t best_addr = {0};   // Store the address of the device with the strongest signal
static esp_ble_addr_type_t best_addr_type = BLE_ADDR_TYPE_PUBLIC;
static int8_t best_rssi = -128;         // Store the RSSI value of the device with the strongest signal, initialized to the weakest signal strength
static bool s_is_reconnecting = false;  // Whether in reconnection mode
static bool s_found_previous_device = false;  // Whether the original device was found in reconnection mode
static ble_diagnostics_t s_diagnostics = {
    .rssi = -128,
    .last_write_status = ESP_GATT_OK,
};

/* Only one profile is stored */
/* Store a single profile */
ble_profile_t s_ble_profile = {
    .conn_id = 0,
    .gattc_if = ESP_GATT_IF_NONE,
    .remote_bda = {0x60, 0x60, 0x1F, 0x60, 0x11, 0xE7},  // Temporarily store the MAC address of the last connected device; you can initialize it with a test value for debugging purposes.
                                                         // Store the last connected MAC address; a test address may be assigned here
    .notify_char_handle = 0,
    .write_char_handle = 0,
    .read_char_handle = 0,
    .service_start_handle = 0,
    .service_end_handle = 0,
    .connection_status = {
        .is_connected = false,
    },
    .handle_discovery = {
        .notify_char_handle_found = false,
        .write_char_handle_found = false,
    },
};

/* Define the Service/Characteristic UUIDs to filter, for search use */
/* Service and characteristic UUID filters used during discovery */
#define REMOTE_TARGET_SERVICE_UUID   0xFFF0
#define REMOTE_LEGACY_CHAR_UUID      0xFFF3
#define REMOTE_NOTIFY_CHAR_UUID      0xFFF4
#define REMOTE_WRITE_CHAR_UUID       0xFFF5

static esp_bt_uuid_t s_filter_legacy_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = REMOTE_LEGACY_CHAR_UUID,
};

static esp_bt_uuid_t s_filter_notify_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = REMOTE_NOTIFY_CHAR_UUID,
};

static esp_bt_uuid_t s_filter_write_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = REMOTE_WRITE_CHAR_UUID,
};

static esp_bt_uuid_t s_notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
};

/* Scan parameters, adjustable as needed */
/* Adjustable scan parameters */
static esp_ble_scan_params_t s_ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

static const char *gatt_type_name(esp_gatt_db_attr_type_t type)
{
    switch (type) {
        case ESP_GATT_DB_PRIMARY_SERVICE: return "SERVICE";
        case ESP_GATT_DB_SECONDARY_SERVICE: return "SECONDARY";
        case ESP_GATT_DB_CHARACTERISTIC: return "CHAR";
        case ESP_GATT_DB_DESCRIPTOR: return "DESCR";
        case ESP_GATT_DB_INCLUDED_SERVICE: return "INCLUDED";
        default: return "ATTR";
    }
}

static void log_gatt_uuid(const esp_gattc_db_elem_t *item)
{
    if (item->uuid.len == ESP_UUID_LEN_16) {
        ESP_LOGI(TAG, "GATT %-9s h=0x%04x range=0x%04x-0x%04x prop=0x%02x uuid=0x%04x",
                 gatt_type_name(item->type), item->attribute_handle,
                 item->start_handle, item->end_handle, item->properties,
                 item->uuid.uuid.uuid16);
    } else if (item->uuid.len == ESP_UUID_LEN_32) {
        ESP_LOGI(TAG, "GATT %-9s h=0x%04x range=0x%04x-0x%04x prop=0x%02x uuid=0x%08lx",
                 gatt_type_name(item->type), item->attribute_handle,
                 item->start_handle, item->end_handle, item->properties,
                 (unsigned long)item->uuid.uuid.uuid32);
    } else {
        ESP_LOGI(TAG, "GATT %-9s h=0x%04x range=0x%04x-0x%04x prop=0x%02x uuid128:",
                 gatt_type_name(item->type), item->attribute_handle,
                 item->start_handle, item->end_handle, item->properties);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, item->uuid.uuid.uuid128, ESP_UUID_LEN_128, ESP_LOG_INFO);
    }
}

static void dump_gatt_database(esp_gatt_if_t gattc_if, uint16_t conn_id)
{
    uint16_t count = 0;
    esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
        gattc_if, conn_id, ESP_GATT_DB_ALL, 1, UINT16_MAX, 0, &count);
    if (status != ESP_GATT_OK || count == 0) {
        ESP_LOGW(TAG, "GATT database unavailable, status=%d count=%u", status, count);
        return;
    }

    esp_gattc_db_elem_t *items = calloc(count, sizeof(*items));
    if (items == NULL) {
        ESP_LOGE(TAG, "No memory for %u GATT attributes", count);
        return;
    }
    status = esp_ble_gattc_get_db(gattc_if, conn_id, 1, UINT16_MAX, items, &count);
    if (status == ESP_GATT_OK) {
        s_diagnostics.attribute_count = count;
        ESP_LOGI(TAG, "ACTION2_DIAG GATT attributes=%u", count);
        for (uint16_t i = 0; i < count; ++i) {
            log_gatt_uuid(&items[i]);
        }
    } else {
        ESP_LOGW(TAG, "GATT database read failed, status=%d", status);
    }
    free(items);
}

/* Callback function declarations */
/* Callback declarations */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param);

static TimerHandle_t scan_timer;
static volatile bool s_scan_finished;
static volatile bool s_scan_stop_requested;

bool ble_connect_attempt_finished(void) {
    return s_scan_finished && !s_connecting && !s_ble_profile.connection_status.is_connected;
}

static void scan_stop_timer_callback(TimerHandle_t timer) {
    (void)timer;
    if (s_scan_stop_requested) return;
    s_scan_stop_requested = true;
    esp_err_t ret = esp_ble_gap_stop_scanning();
    if (ret != ESP_OK) {
        s_scan_stop_requested = false;
        ESP_LOGW(TAG, "Failed to stop scanning: %s", esp_err_to_name(ret));
    }
}

static esp_err_t trigger_scan_task(void) {
    s_scan_finished = false;
    s_scan_stop_requested = false;
    uint32_t scan_ms = s_is_reconnecting ? 1000 : 4000;
    ESP_LOGI(TAG, "esp_ble_gap_start_scanning...");
    // Use one stop mechanism, including early stop when the saved target appears.
    esp_err_t ret = esp_ble_gap_start_scanning(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scanning: %s", esp_err_to_name(ret));
        return ret;
    }

    if (scan_timer == NULL) {
        scan_timer = xTimerCreate("scan_timer", pdMS_TO_TICKS(4000), pdFALSE,
                                  NULL, scan_stop_timer_callback);
    }
    if (scan_timer == NULL || xTimerChangePeriod(scan_timer, pdMS_TO_TICKS(scan_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to arm scan stop timer");
        esp_ble_gap_stop_scanning();
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* -------------------------
 *  Initialization/Scan/Connection related interfaces
 *  Initialization, scanning, and connection interfaces
 * ------------------------- */

/**
 * @brief BLE client initialization
 * Initialize the BLE client
 *
 * @return esp_err_t
 *         - ESP_OK on success
 *         - Others on failure
 */
esp_err_t ble_init() {
    /* Initialize NVS */
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) return ret; // Never erase settings implicitly.

    /* Release classic Bluetooth memory */
    /* Release Classic Bluetooth memory */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* Configure and initialize the Bluetooth controller */
    /* Configure and initialize the Bluetooth controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start the BLE controller */
    /* Start the BLE controller */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize the Bluedroid stack */
    /* Initialize the Bluedroid stack */
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable Bluedroid */
    /* Enable Bluedroid */
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register GAP callback */
    /* Register the GAP callback */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gap register error, err code = %x", ret);
        return ret;
    }

    /* Register GATTC callback */
    /* Register the GATTC callback */
    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "gattc register error, err code = %x", ret);
        return ret;
    }

    /* Register GATTC application (only one profile here, app_id = 0) */
    /* Register the GATTC application (one profile, app_id = 0) */
    ret = esp_ble_gattc_app_register(0);
    if (ret) {
        ESP_LOGE(TAG, "gattc app register error, err code = %x", ret);
        return ret;
    }

    /* Set local MTU (optional) */
    /* Set the local MTU (optional) */
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "ble_init success!");
    return ESP_OK;
}

/**
 * @brief Connect to a device with a specified name (if already scanning, it will automatically connect when the device is found)
 * Connect to a named device; an active scan connects when the device is found
 *
 * @note  This interface is for demonstration only. If you want to actively specify an address to connect, you can extend the interface yourself.
 *        This demo interface can be extended to connect to a specific address
 * @return esp_err_t
 */
esp_err_t ble_start_scanning_and_connect(void) {
    // TODO: Add reconnection logic; current implementation has issues and needs to be fixed.
    // TODO: Complete the reconnection logic; the current implementation needs repair
    if(ble_get_reconnecting()) {
        return ble_reconnect();
    }

    // Reset scan-related variables
    // Reset scan state
    memset(best_addr, 0, sizeof(esp_bd_addr_t));
    best_addr_type = BLE_ADDR_TYPE_PUBLIC;
    best_rssi = -128;
    memset(s_remote_device_name, 0, ESP_BLE_ADV_NAME_LEN_MAX);
    s_is_reconnecting = false;
    s_found_previous_device = false;

    // Set scan parameters
    // Set scan parameters
    esp_err_t ret = esp_ble_gap_set_scan_params(&s_ble_scan_params);
    if (ret) {
        ESP_LOGE(TAG, "Set scan params error: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Set scan params ok!");
    return ESP_OK;
}

static void try_to_connect(esp_bd_addr_t addr) {
    // Check if already connecting
    // Check whether a connection attempt is in progress
    if (s_connecting) {
        ESP_LOGW(TAG, "Already in connecting state, please wait...");
        return;
    }

    // Check if the address is the initial value (all zeros)
    // Check whether the address is still all zeros
    bool is_valid = false;
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
        if (addr[i] != 0) {
            is_valid = true;
            break;
        }
    }

    if (!is_valid) {
        ESP_LOGE(TAG, "Invalid device address (all zeros)");
        return;
    }

    s_connecting = true;
    ESP_LOGI(TAG, "Try to connect target device name = %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_remote_device_name,
             addr[0], addr[1], addr[2],
             addr[3], addr[4], addr[5]);

    // Do not call lightly, if you connect to a non-existent device address, you will have to wait a while before you can connect again
    // Use with care: connecting to an absent device delays subsequent connection attempts
    ESP_LOGI(TAG, "ACTION2_DIAG connect address_type=%u", best_addr_type);
    esp_err_t open_result = esp_ble_gattc_open(s_ble_profile.gattc_if,
                       addr,
                       best_addr_type,
                       true);
    if (open_result != ESP_OK) {
        s_connecting = false;
        ESP_LOGW(TAG, "Could not start connection: %s", esp_err_to_name(open_result));
    }
}

void ble_set_reconnecting(bool flag) {
    s_is_reconnecting = flag;
}

bool ble_get_reconnecting(void) {
    return s_is_reconnecting;
}

void ble_set_reconnect_target(const uint8_t address[ESP_BD_ADDR_LEN], uint8_t address_type) {
    memcpy(best_addr, address, sizeof(best_addr));
    best_addr_type = (esp_ble_addr_type_t)address_type;
    best_rssi = -128;
    s_found_previous_device = false;
}

/**
 * @brief Reconnect to the last connected device
 * Reconnect to the last connected device
 * 
 * @note Only applicable to non-active disconnection situations, as device information is not cleared
 *       Applies to unexpected disconnections, where device information is retained
 * @return esp_err_t
 */
esp_err_t ble_reconnect(void) {
    // Check if there is a valid last connection address
    // Check for a valid previous device address
    bool is_valid = false;
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
        if (best_addr[i] != 0) {
            is_valid = true;
            break;
        }
    }

    if (!is_valid) {
        ESP_LOGE(TAG, "No valid previous device address found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Attempting to reconnect to previous device: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_remote_device_name,
             best_addr[0], best_addr[1], best_addr[2],
             best_addr[3], best_addr[4], best_addr[5]);

    // Set reconnection mode flag
    // Enable reconnection mode
    s_is_reconnecting = true;
    s_found_previous_device = false;  // Reset discovery flag
    
    // Start scan task
    // Start the scan task
    return trigger_scan_task();
}

esp_err_t ble_cancel_pending_connect(void) {
    if (!s_connecting) {
        return ESP_OK;
    }

    esp_ble_gattc_cancel_open_params_t params = {
        .gattc_if = s_ble_profile.gattc_if,
    };
    memcpy(params.remote_bda, best_addr, sizeof(params.remote_bda));
    esp_err_t ret = esp_ble_gattc_cancel_open(&params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to cancel pending connection: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < 50 && s_connecting; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_connecting) {
        ESP_LOGE(TAG, "Timed out cancelling pending connection");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/**
 * @brief Disconnect (if connected)
 * Disconnect if connected
 *
 * @return esp_err_t
 */
esp_err_t ble_disconnect(void) {
    if (s_ble_profile.connection_status.is_connected) {
        esp_ble_gattc_close(s_ble_profile.gattc_if, s_ble_profile.conn_id);
    }
    return ESP_OK;
}

/* -------------------------
 *  Read/Write and Notify related interfaces
 *  Read, write, and notification interfaces
 * ------------------------- */
/**
 * @brief Read a specified characteristic
 * Read the specified characteristic
 *
 * @param conn_id  Connection ID (obtained from callback events or internal management)
 *                 Connection ID obtained from callbacks or internal state
 * @param handle   Handle of the characteristic
 *                 Characteristic handle
 * @return esp_err_t
 */
esp_err_t ble_read(uint16_t conn_id, uint16_t handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip read");
        return ESP_FAIL;
    }
    /* Initiate GATTC read request */
    /* Issue a GATTC read request */
    esp_err_t ret = esp_ble_gattc_read_char(s_ble_profile.gattc_if,
                                            conn_id,
                                            handle,
                                            ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "read_char failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write characteristic (Write Without Response)
 * Write a characteristic without response
 *
 * @param conn_id   Connection ID
 *                  Connection ID
 * @param handle    Handle of the characteristic
 *                  Characteristic handle
 * @param data      Data to be written
 *                  Data to write
 * @param length    Length of the data
 *                  Data length
 * @return esp_err_t
 */
esp_err_t ble_write_without_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip write_without_response");
        return ESP_FAIL;
    }
    esp_err_t ret = esp_ble_gattc_write_char(s_ble_profile.gattc_if,
                                             conn_id,
                                             handle,
                                             length,
                                             (uint8_t *)data,
                                             ESP_GATT_WRITE_TYPE_NO_RSP,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "write_char NO_RSP failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write characteristic (Write With Response)
 * Write a characteristic with response
 *
 * @param conn_id   Connection ID
 *                  Connection ID
 * @param handle    Handle of the characteristic
 *                  Characteristic handle
 * @param data      Data to be written
 *                  Data to write
 * @param length    Length of the data
 *                  Data length
 * @return esp_err_t
 */
esp_err_t ble_write_with_response(uint16_t conn_id, uint16_t handle, const uint8_t *data, size_t length) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip write_with_response");
        return ESP_FAIL;
    }
    esp_err_t ret = esp_ble_gattc_write_char(s_ble_profile.gattc_if,
                                             conn_id,
                                             handle,
                                             length,
                                             (uint8_t *)data,
                                             ESP_GATT_WRITE_TYPE_RSP,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (ret) {
        ESP_LOGE(TAG, "write_char RSP failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Register (enable) Notify
 * Register for notifications
 *
 * @param conn_id   Connection ID
 *                  Connection ID
 * @param char_handle Handle of the characteristic to enable notification
 *                    Handle of the characteristic to enable notifications on
 * @return esp_err_t
 */
esp_err_t ble_register_notify(uint16_t conn_id, uint16_t char_handle) {
    if (!s_ble_profile.connection_status.is_connected) {
        ESP_LOGW(TAG, "Not connected, skip register_notify");
        return ESP_FAIL;
    }
    /* Request to subscribe to notifications from the protocol stack */
    /* Ask the stack to subscribe to notifications */
    esp_err_t ret = esp_ble_gattc_register_for_notify(s_ble_profile.gattc_if,
                                                      s_ble_profile.remote_bda,
                                                      char_handle);
    if (ret) {
        ESP_LOGE(TAG, "register_notify failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Unregister (disable) Notify
 * Unregister notifications
 *
 * @note  This is just a demonstration logic. You need the Client Config descriptor handle of the characteristic to operate.
 *        If needed in actual development, you can directly save the descr handle previously, and then close it by writing 0x0000 here.
 *        This example requires the characteristic's client configuration descriptor handle
 *        Alternatively, retain the descriptor handle and write 0x0000 here to disable notifications
 *
 * @param conn_id   Connection ID
 *                  Connection ID
 * @param char_handle Handle of the characteristic to disable notification
 *                    Handle of the characteristic to disable notifications on
 * @return esp_err_t
 */
esp_err_t ble_unregister_notify(uint16_t conn_id, uint16_t char_handle) {
    /* In fact, you need to get the corresponding descriptor handle and then write 0x0000 to disable it */
    /* Obtain the corresponding descriptor handle and write 0x0000 to disable notifications */
    /* This is just a demonstration of the process. If needed, you can save the descr handle during register_notify */
    /* This demonstrates the flow; retain the descriptor handle in register_notify if needed */
    ESP_LOGI(TAG, "ble_unregister_notify called (demo), not fully implemented");
    return ESP_OK;
}

/**
 * @brief Set global Notify callback (for receiving data)
 * Set the global callback for received notifications
 *
 * @param cb Callback function pointer
 *           Callback function pointer
 */
void ble_set_notify_callback(ble_notify_callback_t cb) {
    s_notify_cb = cb;
}

/**
 * @brief Set global logic layer disconnection state callback
 * Set the global logic-layer disconnection callback
 *
 * @param cb Callback function pointer
 *           Callback function pointer
 */
void ble_set_state_callback(connect_logic_state_callback_t cb) {
    s_state_cb = cb;
}

void ble_get_diagnostics(ble_diagnostics_t *diagnostics) {
    if (diagnostics != NULL) {
        *diagnostics = s_diagnostics;
    }
}

/* ----------------------------------------------------------------
 *   GAP & GATTC callback function implementation (simplified version)
 *   GAP and GATTC callback implementations (simplified)
 * ---------------------------------------------------------------- */

/* Determine whether this is a DJI camera advertisement */
/* Determine whether it is a DJI camera advertisement */
static uint8_t bsp_link_is_dji_camera_adv(esp_ble_gap_cb_param_t *scan_result) {
    const uint8_t *ble_adv = scan_result->scan_rst.ble_adv;
    const uint8_t adv_len = scan_result->scan_rst.adv_data_len + scan_result->scan_rst.scan_rsp_len;

    for (int i = 0; i < adv_len; ) {
        const uint8_t len = ble_adv[i];
        
        if (len == 0 || (i + len + 1) > adv_len) break;

        const uint8_t type = ble_adv[i+1];
        const uint8_t *data = &ble_adv[i+2];
        const uint8_t data_len = len - 1;

        if (type == ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE) {
            if (data_len >= 5) {
                if (data[0] == 0xAA && data[1] == 0x08 && data[4] == 0xFA) {
                    return 1;
                }
            }
        }
        i += (len + 1);
    }
    return 0;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
        trigger_scan_task();
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (scan_timer != NULL) xTimerStop(scan_timer, 0);
        ESP_LOGI(TAG, "scan stopped");
        // After scanning ends, decide whether to connect based on reconnection mode and device discovery status
        // After scanning, decide whether to connect based on reconnection mode and discovery results
        if (best_rssi > -128) {
            if (!ble_get_reconnecting() || (ble_get_reconnecting() && s_found_previous_device)) {
                try_to_connect(best_addr);
                ESP_LOGI(TAG, "Connected to device: %02x:%02x:%02x:%02x:%02x:%02x",
                         best_addr[0], best_addr[1], best_addr[2], best_addr[3], best_addr[4], best_addr[5]);
            } else {
                ESP_LOGW(TAG, "In reconnection mode but target device not found");
            }
        } else {
            ESP_LOGW(TAG, "No suitable device found with sufficient signal strength");
        }
        s_is_reconnecting = false;
        s_scan_finished = true;
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *r = param;
        if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            // Check if it is a DJI camera advertisement
            // Check for a DJI camera advertisement
            if (!bsp_link_is_dji_camera_adv(r)) {
                break;
            }
            // Get the complete name from the advertisement data
            // Get the complete name from the advertisement
            uint8_t *adv_name = NULL;
            uint8_t adv_name_len = 0;
            adv_name = esp_ble_resolve_adv_data_by_type(r->scan_rst.ble_adv,
                                r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                                ESP_BLE_AD_TYPE_NAME_CMPL,
                                &adv_name_len);

            // Prepare a safe string pointer for logging
            // Prepare a safe string pointer for logging
            const char *adv_name_str = NULL;
            if (adv_name && adv_name_len > 0) {
                static char name_buf[64];
                size_t copy_len = adv_name_len < sizeof(name_buf) - 1 ? adv_name_len : sizeof(name_buf) - 1;
                memcpy(name_buf, adv_name, copy_len);
                name_buf[copy_len] = '\0';
                adv_name_str = name_buf;
            } else {
                adv_name_str = "NULL";
            }

            ESP_LOGI(TAG, "Found device: %s with RSSI: %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                adv_name_str,
                r->scan_rst.rssi,
                r->scan_rst.bda[0], r->scan_rst.bda[1], r->scan_rst.bda[2],
                r->scan_rst.bda[3], r->scan_rst.bda[4], r->scan_rst.bda[5]);
            ESP_LOGI(TAG, "ACTION2_DIAG address_type=%u adv_len=%u scan_rsp_len=%u",
                     r->scan_rst.ble_addr_type, r->scan_rst.adv_data_len,
                     r->scan_rst.scan_rsp_len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, r->scan_rst.ble_adv,
                                     r->scan_rst.adv_data_len + r->scan_rst.scan_rsp_len,
                                     ESP_LOG_INFO);

            // Compare names and record signal strength
            // Compare the name and record signal strength
            if (ble_get_reconnecting()) {
                // In reconnection mode, compare device addresses
                // Match the device address in reconnection mode
                if (memcmp(best_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t)) == 0) {
                    s_found_previous_device = true;
                    best_rssi = r->scan_rst.rssi;
                    best_addr_type = r->scan_rst.ble_addr_type;
                    s_diagnostics.rssi = r->scan_rst.rssi;
                    s_diagnostics.address_type = r->scan_rst.ble_addr_type;
                    strncpy(s_remote_device_name, adv_name_str, sizeof(s_remote_device_name) - 1);
                    s_remote_device_name[sizeof(s_remote_device_name) - 1] = '\0';
                    ESP_LOGI(TAG, "Found previous device: %s, RSSI: %d", adv_name_str, r->scan_rst.rssi);
                    scan_stop_timer_callback(NULL);
                }
            } else {
                // In normal scan mode, record the device with the strongest signal
                // In normal scan mode, retain the device with the strongest signal
                if (r->scan_rst.rssi > best_rssi && r->scan_rst.rssi >= MIN_RSSI_THRESHOLD) {
                    best_rssi = r->scan_rst.rssi;
                    best_addr_type = r->scan_rst.ble_addr_type;
                    s_diagnostics.rssi = r->scan_rst.rssi;
                    s_diagnostics.address_type = r->scan_rst.ble_addr_type;
                    memcpy(best_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));
                    strncpy(s_remote_device_name, adv_name_str, sizeof(s_remote_device_name) - 1);
                    s_remote_device_name[sizeof(s_remote_device_name) - 1] = '\0';
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
    switch (event) {
    case ESP_GATTC_REG_EVT: {
        // Handle GATT client registration event
        // Handle GATT client registration
        if (param->reg.status == ESP_GATT_OK) {
            s_ble_profile.gattc_if = gattc_if;
            ESP_LOGI(TAG, "GATTC register OK, app_id=%d, gattc_if=%d",
                     param->reg.app_id, gattc_if);
        } else {
            ESP_LOGE(TAG, "GATTC register failed, status=%d", param->reg.status);
        }
        break;
    }
    case ESP_GATTC_CONNECT_EVT: {
        // Handle connection event
        // Handle the connection event
        s_ble_profile.conn_id = param->connect.conn_id;
        s_ble_profile.connection_status.is_connected = true;
        memcpy(s_ble_profile.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "Connected, conn_id=%d", s_ble_profile.conn_id);
        s_diagnostics.mtu = 0;
        s_diagnostics.service_count = 0;
        s_diagnostics.attribute_count = 0;
        s_diagnostics.notification_count = 0;
        s_diagnostics.target_service_found = false;

        ESP_LOGI(TAG, "Connect to camera MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
            param->connect.remote_bda[0],
            param->connect.remote_bda[1],
            param->connect.remote_bda[2],
            param->connect.remote_bda[3],
            param->connect.remote_bda[4],
            param->connect.remote_bda[5]);

        // Initiate MTU request
        // Request the MTU
        esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
        break;
    }
    case ESP_GATTC_OPEN_EVT: {
        // Handle connection open event
        // Handle the connection-open event
        s_connecting = false;
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Open failed, status=%d", param->open.status);
            break;
        }
        ESP_LOGI(TAG, "Open success, MTU=%u", param->open.mtu);
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT: {
        // Handle MTU configuration event
        // Handle MTU configuration
        if (param->cfg_mtu.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Config MTU Error, status=%d", param->cfg_mtu.status);
        }
        ESP_LOGI(TAG, "MTU=%d", param->cfg_mtu.mtu);
        s_diagnostics.mtu = param->cfg_mtu.mtu;

        // Start service discovery after MTU configuration
        // Start service discovery after MTU configuration
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT: {
        // Handle service search result event
        // Handle service discovery results
        ++s_diagnostics.service_count;
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) {
            ESP_LOGI(TAG, "ACTION2_DIAG service uuid=0x%04x range=0x%04x-0x%04x",
                     param->search_res.srvc_id.uuid.uuid.uuid16,
                     param->search_res.start_handle, param->search_res.end_handle);
        } else {
            ESP_LOGI(TAG, "ACTION2_DIAG service uuid128 range=0x%04x-0x%04x",
                     param->search_res.start_handle, param->search_res.end_handle);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->search_res.srvc_id.uuid.uuid.uuid128,
                                     ESP_UUID_LEN_128, ESP_LOG_INFO);
        }
        if ((param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) &&
            (param->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_TARGET_SERVICE_UUID)) {
            s_diagnostics.target_service_found = true;
            s_ble_profile.service_start_handle = param->search_res.start_handle;
            s_ble_profile.service_end_handle   = param->search_res.end_handle;
            ESP_LOGI(TAG, "Service found: start=%d, end=%d",
                     s_ble_profile.service_start_handle,
                     s_ble_profile.service_end_handle);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
        // Handle service search complete event
        // Handle completion of service discovery
        if (param->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Service search failed, status=%d", param->search_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Service search complete, next get char by UUID");
        dump_gatt_database(gattc_if, s_ble_profile.conn_id);

        // Get notify characteristic handle
        // Get the notification characteristic handle
        uint16_t count = 1;
        esp_gattc_char_elem_t char_elem_result;
        esp_ble_gattc_get_char_by_uuid(gattc_if,
                                       s_ble_profile.conn_id,
                                       s_ble_profile.service_start_handle,
                                       s_ble_profile.service_end_handle,
                                       s_filter_notify_char_uuid,
                                       &char_elem_result,
                                       &count);
        if (count > 0) {
            s_ble_profile.notify_char_handle = char_elem_result.char_handle;
            s_ble_profile.handle_discovery.notify_char_handle_found = true;
            ESP_LOGI(TAG, "Notify Char found, handle=0x%x",
                     s_ble_profile.notify_char_handle);
        }

        count = 1;
        esp_gattc_char_elem_t legacy_char_elem_result;
        esp_ble_gattc_get_char_by_uuid(gattc_if,
                                       s_ble_profile.conn_id,
                                       s_ble_profile.service_start_handle,
                                       s_ble_profile.service_end_handle,
                                       s_filter_legacy_char_uuid,
                                       &legacy_char_elem_result,
                                       &count);
        if (count > 0) {
            s_ble_profile.read_char_handle = legacy_char_elem_result.char_handle;
            ESP_LOGI(TAG, "Legacy Char found, handle=0x%x",
                     s_ble_profile.read_char_handle);
        }

        // Get write characteristic handle
        // Get the write characteristic handle
        count = 1;
        esp_gattc_char_elem_t write_char_elem_result;
        esp_ble_gattc_get_char_by_uuid(gattc_if,
                                       s_ble_profile.conn_id,
                                       s_ble_profile.service_start_handle,
                                       s_ble_profile.service_end_handle,
                                       s_filter_write_char_uuid,
                                       &write_char_elem_result,
                                       &count);
        if (count > 0) {
            s_ble_profile.write_char_handle = write_char_elem_result.char_handle;
            s_ble_profile.handle_discovery.write_char_handle_found = true;
            ESP_LOGI(TAG, "Write Char found, handle=0x%x",
                     s_ble_profile.write_char_handle);
        }

        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        // Handle notification registration event
        // Handle notification registration
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Notify register failed, status=%d", param->reg_for_notify.status);
            break;
        }
        ESP_LOGI(TAG, "Notify register success, handle=0x%x", param->reg_for_notify.handle);

        // Find descriptor and write 0x01 to enable notification
        // Find the descriptor and write 0x01 to enable notifications
        uint16_t count = 1;
        esp_gattc_descr_elem_t descr_elem;
        esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                               s_ble_profile.conn_id,
                                               param->reg_for_notify.handle,
                                               s_notify_descr_uuid,
                                               &descr_elem,
                                               &count);
        if (count > 0 && descr_elem.handle) {
            uint16_t notify_en = 1;
            esp_ble_gattc_write_char_descr(gattc_if,
                                           s_ble_profile.conn_id,
                                           descr_elem.handle,
                                           sizeof(notify_en),
                                           (uint8_t *)&notify_en,
                                           ESP_GATT_WRITE_TYPE_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
        // Handle notification data event
        // Handle notification data
        ++s_diagnostics.notification_count;
        ESP_LOGI(TAG, "ACTION2_DIAG RX notify handle=0x%04x len=%u indication=%u",
                 param->notify.handle, param->notify.value_len, param->notify.is_notify ? 0 : 1);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->notify.value, param->notify.value_len, ESP_LOG_INFO);
        if (s_notify_cb) {
            s_notify_cb(param->notify.value, param->notify.value_len);
        }

        break;
    }
    case ESP_GATTC_CANCEL_OPEN_EVT:
        s_connecting = false;
        ESP_LOGI(TAG, "Pending connection cancelled, status=%d", param->cancel_open.status);
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        s_diagnostics.last_write_status = param->write.status;
        ESP_LOGI(TAG, "ACTION2_DIAG write char handle=0x%04x status=%d offset=%u",
                 param->write.handle, param->write.status, param->write.offset);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(TAG, "ACTION2_DIAG write descriptor handle=0x%04x status=%d offset=%u",
                 param->write.handle, param->write.status, param->write.offset);
        break;
    case ESP_GATTC_READ_CHAR_EVT:
        ESP_LOGI(TAG, "ACTION2_DIAG read char handle=0x%04x status=%d len=%u",
                 param->read.handle, param->read.status, param->read.value_len);
        if (param->read.status == ESP_GATT_OK && param->read.value_len > 0) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->read.value, param->read.value_len, ESP_LOG_INFO);
        }
        break;
    case ESP_GATTC_DISCONNECT_EVT: {
        // Handle disconnection event
        // Handle disconnection
        s_ble_profile.connection_status.is_connected = false;
        s_ble_profile.handle_discovery.write_char_handle_found = false;
        s_ble_profile.handle_discovery.notify_char_handle_found = false;
        s_connecting = false;
        ESP_LOGI(TAG, "Disconnected, reason=0x%x", param->disconnect.reason);

        if (s_state_cb) {
            s_state_cb();
        }
        break;
    }
    default:
        break;
    }
}

// BLE Advertising Data Format
// Advertisement data
static uint8_t adv_data[] = {
    10, 0xff, 'W','K','P','1','2','3','4','5','6'
};

static esp_timer_handle_t adv_timer;

static void stop_adv_after_2s(void* arg) {
    esp_ble_gap_stop_advertising();
    ESP_LOGI(TAG, "Advertising stopped after 2 seconds");
}

esp_err_t ble_start_advertising() {
    // Check if remote_bda is initialized
    // Check whether remote_bda has been initialized
    if (memcmp(s_ble_profile.remote_bda, "\x00\x00\x00\x00\x00\x00", 6) == 0) {
        ESP_LOGE(TAG, "Error: remote_bda is not initialized!");
        ESP_LOGE(TAG, "Error: remote_bda not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 6; i++) {
        // adv_data[8 + i] = s_ble_profile.remote_bda[5 - i];
        adv_data[5 + i] = s_ble_profile.remote_bda[5 - i];
    }

    ESP_LOGI(TAG, "Modified Advertising Data (with MAC):");
    ESP_LOG_BUFFER_HEX(TAG, adv_data, sizeof(adv_data));

    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .channel_map = ADV_CHNL_ALL,
    };

    esp_err_t ret = esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set adv data.");
        return ret;
    }

    ret = esp_ble_gap_start_advertising(&adv_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start advertising: %s", esp_err_to_name(ret));
        return ret;
    }

    if (adv_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &stop_adv_after_2s,
            .name = "adv_timer"
        };
        esp_timer_create(&timer_args, &adv_timer);
    }
    
    esp_timer_start_once(adv_timer, 2000000);  // 2000ms = 2,000,000us

    ESP_LOGI(TAG, "Advertising started (will auto-stop after 2s)");
    return ESP_OK;
}
