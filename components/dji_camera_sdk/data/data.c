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
 * If you receive this source code without DJI's authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 */

#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "data.h"
#include "ble.h"
#include "dji_protocol_parser.h"

#define TAG "DATA"

/* Maximum number of concurrently pending commands */
/* Maximum number of commands that can be waited in parallel */
#define MAX_SEQ_ENTRIES 10

/* Periodic cleanup interval in milliseconds */
/* Cleanup interval in milliseconds */
#define CLEANUP_INTERVAL_MS 60000

/* Maximum retention time in seconds; remove entries unused beyond this limit */
/* Maximum retention time in seconds, entries unused beyond this time will be cleared */
#define MAX_ENTRY_AGE 120

static bool data_layer_initialized = false;

/* Entry structure */
/* Entry structure */
typedef struct {
    // Whether the entry is valid
    // Whether the entry is valid
    bool in_use;

    // True for seq-based entries; false for cmd_set/cmd_id-based entries
    // Added: true means based on seq, false means based on cmd_set and cmd_id
    bool is_seq_based;

    // Valid when is_seq_based is true
    // Valid if is_seq_based is true
    uint16_t seq;

    // Valid when is_seq_based is false
    // Valid if is_seq_based is false
    uint8_t cmd_set;

    // Valid when is_seq_based is false
    // Valid if is_seq_based is false
    uint8_t cmd_id;

    // Parsed generic structure
    // Generic structure after parsing
    void *parse_result;

    // Length of the parsed result
    // Length of parsed result
    size_t parse_result_length;

    // Used for synchronous waiting
    // For synchronous waiting
    SemaphoreHandle_t sem;

    // Last-access timestamp for LRU eviction
    // Last access timestamp for LRU policy
    TickType_t last_access_time;
} entry_t;

/* Mapping from sequence numbers to parsed results */
/* Maintains mapping from seq to parsed results */
static entry_t s_entries[MAX_SEQ_ENTRIES];

/* Mutex protecting s_seq_entries */
/* Mutex to protect s_seq_entries */
static SemaphoreHandle_t s_map_mutex = NULL;

/* Timer handle */
/* Timer handle */
static TimerHandle_t cleanup_timer = NULL;

/* Task handle for deferred notification processing */
/* Task handle for delayed notification processing */
static TaskHandle_t notify_task_handle = NULL;

/* Notification data queue */
/* Queue for notification data */
static QueueHandle_t notify_queue = NULL;

/* Notification data structure */
/* Structure for notification data */
typedef struct {
    uint8_t *data;
    size_t data_length;
} notify_data_t;

/* Forward declarations */
/* Forward declarations */
static void notify_processing_task(void *pvParameters);
static void process_notification_data(const uint8_t *raw_data, size_t raw_data_length);

/**
 * @brief Initialize seq_entries and mark all entries as unused
 *        Initialize seq_entries and mark all entries as unused
 */
static void reset_entries(void) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        s_entries[i].in_use = false;
        s_entries[i].is_seq_based = false;
        s_entries[i].seq = 0;
        s_entries[i].cmd_set = 0;
        s_entries[i].cmd_id = 0;
        s_entries[i].last_access_time = 0;
        if (s_entries[i].parse_result) {
            free(s_entries[i].parse_result);
            s_entries[i].parse_result = NULL;
        }
        s_entries[i].parse_result_length = 0;
        if (s_entries[i].sem) {
            vSemaphoreDelete(s_entries[i].sem);
            s_entries[i].sem = NULL;
        }
    }
}

/**
 * @brief Find entry by sequence number
 *        Find the entry for a sequence number
 * 
 * @param seq Sequence number to find
 *            Sequence number to find
 * @return entry_t* Pointer to found entry, NULL if not found
 *                  Pointer to the matching entry, or NULL if not found
 */
static entry_t* find_entry_by_seq(uint16_t seq) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && s_entries[i].is_seq_based && s_entries[i].seq == seq) {
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Find entry by command set and ID
 *        Find the entry for cmd_set and cmd_id
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @return entry_t* Pointer to found entry, NULL if not found
 *                  Pointer to the matching entry, or NULL if not found
 */
static entry_t* find_entry_by_cmd_id(uint16_t cmd_set, uint16_t cmd_id) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && !s_entries[i].is_seq_based && 
            s_entries[i].cmd_set == cmd_set && s_entries[i].cmd_id == cmd_id) {
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Free an entry
 *        Free an entry
 * 
 * @param entry Pointer to the entry to be freed
 *              Pointer to the entry to free
 */
static void free_entry(entry_t *entry) {
    if (entry) {
        entry->in_use = false;
        entry->is_seq_based = false;
        entry->seq = 0;
        entry->cmd_set = 0;
        entry->cmd_id = 0;
        entry->last_access_time = 0;
        if (entry->parse_result) {
            free(entry->parse_result);
            entry->parse_result = NULL;
        }
        entry->parse_result_length = 0;
        if (entry->sem) {
            vSemaphoreDelete(entry->sem);
            entry->sem = NULL;
        }
    }
}

/**
 * @brief Allocate a free entry based on sequence number
 *        Allocate a free entry keyed by sequence number
 * 
 * @param seq Frame sequence number
 *            Frame sequence number
 * @return entry_t* Pointer to allocated entry, NULL if failed
 *                  Pointer to the allocated entry, or NULL on failure
 */
static entry_t* allocate_entry_by_seq(uint16_t seq) {
    // First check if an entry with the same seq exists
    // First check for an entry with the same sequence number
    entry_t *existing_entry = find_entry_by_seq(seq);
    if (existing_entry) {
        ESP_LOGI(TAG, "Overwriting existing entry for seq=0x%04X", seq);
        free_entry(existing_entry);
    }

    // For tracking the least recently used entry
    // Track the least recently used entry
    entry_t* oldest_entry = NULL;

    // Initialize with current time
    // Initialize the timestamp to the current time
    TickType_t oldest_access_time = xTaskGetTickCount();

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = true;
            s_entries[i].seq = seq;
            s_entries[i].cmd_set = 0;
            s_entries[i].cmd_id = 0;
            s_entries[i].parse_result = NULL;
            s_entries[i].parse_result_length = 0;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }

        // Track the least recently used entry
        // Least recently used entry
        if (s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // If no free entry, delete the least recently used entry
    // Evict the least recently used entry if no free entry is available
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used entry: seq=0x%04X or cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->is_seq_based ? oldest_entry->seq : 0,
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);
        // Reallocate
        // Reallocate
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = true;
        oldest_entry->seq = seq;
        oldest_entry->cmd_set = 0;
        oldest_entry->cmd_id = 0;
        oldest_entry->parse_result = NULL;
        oldest_entry->parse_result_length = 0;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for seq=0x%04X", seq);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();
        return oldest_entry;
    }

    return NULL;
}

/**
 * @brief Allocate a free entry based on command set and ID
 *        Allocate a free entry keyed by cmd_set and cmd_id
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @return entry_t* Pointer to allocated entry, NULL if failed
 *                  Pointer to the allocated entry, or NULL on failure
 */
static entry_t* allocate_entry_by_cmd(uint8_t cmd_set, uint8_t cmd_id) {
    // First check if an entry with the same cmd_set and cmd_id exists
    // First check for an entry with the same cmd_set and cmd_id
    entry_t *existing_entry = find_entry_by_cmd_id(cmd_set, cmd_id);
    if (existing_entry) {
        // Entry exists, reuse it
        // Reuse the existing entry
        ESP_LOGI(TAG, "Entry for cmd_set=0x%04X cmd_id=0x%04X already exists, it will be overwritten", cmd_set, cmd_id);
        return existing_entry;
    }

    // Allocate new entry
    // Allocate a new entry
    entry_t* oldest_entry = NULL;  // For tracking the least recently used non-seq-based entry
                                  // Track the least recently used entry not keyed by sequence number
    TickType_t oldest_access_time = xTaskGetTickCount();

    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!s_entries[i].in_use) {
            // Found a free entry
            // Found a free entry
            s_entries[i].in_use = true;
            s_entries[i].is_seq_based = false;
            s_entries[i].seq = 0;
            s_entries[i].cmd_set = cmd_set;
            s_entries[i].cmd_id = cmd_id;
            s_entries[i].parse_result = NULL;
            s_entries[i].parse_result_length = 0;
            s_entries[i].sem = xSemaphoreCreateBinary();
            if (s_entries[i].sem == NULL) {
                ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
                s_entries[i].in_use = false;
                return NULL;
            }
            s_entries[i].last_access_time = xTaskGetTickCount();
            return &s_entries[i];
        }

        // Only consider non-seq-based entries as deletion candidates
        // Only entries not keyed by sequence number are eligible for eviction
        if (!s_entries[i].is_seq_based && s_entries[i].last_access_time < oldest_access_time) {
            oldest_access_time = s_entries[i].last_access_time;
            oldest_entry = &s_entries[i];
        }
    }

    // If no free entry, try to delete the least recently used non-seq-based entry
    // If no free entry exists, evict the least recently used non-sequence entry
    if (oldest_entry) {
        ESP_LOGW(TAG, "Deleting the least recently used cmd-based entry: cmd_set=0x%04X cmd_id=0x%04X",
                 oldest_entry->cmd_set,
                 oldest_entry->cmd_id);
        free_entry(oldest_entry);

        // Reallocate the deleted entry
        // Reuse the evicted entry
        oldest_entry->in_use = true;
        oldest_entry->is_seq_based = false;
        oldest_entry->seq = 0;
        oldest_entry->cmd_set = cmd_set;
        oldest_entry->cmd_id = cmd_id;
        oldest_entry->parse_result = NULL;
        oldest_entry->parse_result_length = 0;
        oldest_entry->sem = xSemaphoreCreateBinary();
        if (oldest_entry->sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
            oldest_entry->in_use = false;
            return NULL;
        }
        oldest_entry->last_access_time = xTaskGetTickCount();
        return oldest_entry;
    }

    ESP_LOGE(TAG, "No available cmd-based entry to allocate for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
    return NULL;
}

/**
 * @brief Timer cleanup function
 *        Periodic cleanup callback
 * 
 * Clean up expired entries and delete unused entries.
 * Periodically run cleanup tasks to free up memory that is no longer needed.
 * Remove expired entries that are no longer in use
 * Run cleanup periodically to release memory no longer needed
 * 
 * @param xTimer Timer handle that triggered this callback
 *              Handle of the timer invoking this callback
 */
static void cleanup_old_entries(TimerHandle_t xTimer) {
    // Get current system tick count
    // Get the current system tick count
    TickType_t current_time = xTaskGetTickCount();
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in cleanup");
        return;
    }
    // Check each entry for expiration
    // Check each entry for expiration
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (s_entries[i].in_use && (current_time - s_entries[i].last_access_time) > pdMS_TO_TICKS(MAX_ENTRY_AGE * 1000)) {
            if (s_entries[i].is_seq_based) {
                ESP_LOGI(TAG, "Cleaning up unused entry seq=0x%04X", s_entries[i].seq);
            } else {
                ESP_LOGI(TAG, "Cleaning up unused entry cmd_set=0x%04X cmd_id=0x%04X", s_entries[i].cmd_set, s_entries[i].cmd_id);
            }
            free_entry(&s_entries[i]);
        }
    }
    xSemaphoreGive(s_map_mutex);
}

/**
 * @brief Data layer initialization
 *        Initialize the data layer
 * 
 * Initialize data layer, including creating mutex, clearing entries, starting cleanup timer task, etc.
 * Create the mutex, clear entries, and start periodic cleanup
 */
void data_init(void) {
    // Initialize mutex
    // Initialize the mutex
    s_map_mutex = xSemaphoreCreateMutex();
    if (s_map_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Clear all entries
    // Clear all entries
    reset_entries();

    // Initialize timer for cleaning up expired entries
    // Initialize the expiration cleanup timer
    cleanup_timer = xTimerCreate("cleanup_timer", pdMS_TO_TICKS(CLEANUP_INTERVAL_MS), pdTRUE, NULL, cleanup_old_entries);
    if (cleanup_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create cleanup timer");
    } else {
        xTimerStart(cleanup_timer, 0);
    }

    // Initialize notification queue
    // Initialize the notification queue
    notify_queue = xQueueCreate(MAX_SEQ_ENTRIES, sizeof(notify_data_t));
    if (notify_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create notification queue");
    }

    // Initialize notification task
    // Initialize the notification task
    if (xTaskCreate(notify_processing_task, "notify_processing_task", 2048, NULL, 1, &notify_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create notification processing task");
    }

    // Mark data layer as initialized
    // Mark the data layer as initialized
    data_layer_initialized = true;
    ESP_LOGI(TAG, "Data layer initialized successfully");
}

/**
 * @brief Check if data layer is initialized
 *        Check whether the data layer is initialized
 * 
 * @return bool Returns true if data layer is initialized, false otherwise
 *              Return true if the data layer is initialized; otherwise return false
 */
bool is_data_layer_initialized(void) {
    return data_layer_initialized;
}

/**
 * @brief Send data frame with response
 *        Send a data frame with response
 * 
 * Send data frame to device via BLE and wait for response.
 * Send a frame over BLE and wait for a response
 * 
 * @param seq Frame sequence number
 *            Data frame sequence number
 * @param raw_data Data to be sent
 *                 Data to send
 * @param raw_data_length Length of data
 *                        Data length
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                   ESP_OK on success; an error code on failure
 */
esp_err_t data_write_with_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // Validate input arguments
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }

    // Take mutex for thread safety
    // Acquire the mutex for thread safety
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate an entry for this sequence
    // Allocate an entry for this sequence number
    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // Send write command with response
    // Send a write command with response
    ESP_LOGI(TAG, "ACTION2_DIAG TX seq=%u len=%u", seq, (unsigned)raw_data_length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, raw_data, raw_data_length, ESP_LOG_INFO);
    esp_err_t ret = ble_write_with_response(
        s_ble_profile.conn_id,           // Current connection ID
                                         // Current connection ID
        s_ble_profile.write_char_handle, // Write characteristic handle
                                         // Write characteristic handle
        raw_data,                        // Data to be sent
                                         // Data to send
        raw_data_length                  // Length of data
                                         // Data length
    );

    // Handle write failure
    // Handle a failed write
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_with_response failed: %s", esp_err_to_name(ret));
        // Clean up on failure
        // Release resources on failure
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Send data frame without response
 *        Send a data frame without response
 * 
 * Send data frame to device via BLE without waiting for response.
 * Send a frame over BLE without waiting for a response
 * 
 * @param seq Frame sequence number
 *            Data frame sequence number
 * @param raw_data Data to be sent
 *                 Data to send
 * @param raw_data_length Length of data
 *                        Data length
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                   ESP_OK on success; an error code on failure
 */
esp_err_t data_write_without_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // Validate input arguments
    if (!raw_data || raw_data_length == 0) {
        ESP_LOGE(TAG, "Invalid raw_data or raw_data_length");
        return ESP_ERR_INVALID_ARG;
    }

    // Take mutex for thread safety
    // Acquire the mutex for thread safety
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate an entry for this sequence
    // Allocate an entry for this sequence number
    entry_t *entry = allocate_entry_by_seq(seq);
    if (!entry) {
        ESP_LOGE(TAG, "No free entry, can't write");
        xSemaphoreGive(s_map_mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_map_mutex);

    // Send write command without response
    // Send a write command without response
    esp_err_t ret = ble_write_without_response(
        s_ble_profile.conn_id,           // Current connection ID
                                         // Current connection ID
        s_ble_profile.write_char_handle, // Write characteristic handle
                                         // Write characteristic handle
        raw_data,                        // Data to be sent
                                         // Data to send
        raw_data_length                  // Length of data
                                         // Data length
    );

    // Handle write failure
    // Handle a failed write
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_write_without_response failed: %s", esp_err_to_name(ret));
        // Clean up on failure
        // Release resources on failure
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);
        }
        return ret;
    }

    // For write without response, release entry immediately
    // Release the entry immediately for a write without response
    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        free_entry(entry);
        xSemaphoreGive(s_map_mutex);
    }

    return ESP_OK;
}

/**
 * @brief Wait for parsing result of specific sequence number
 *        Wait for the parsed result of a specific sequence number
 * 
 * Wait for parsing result of a specific sequence number and return to caller.
 * Wait for the specified sequence number and return its parsed result to the caller
 * 
 * @param seq Frame sequence number
 *            Data frame sequence number
 * @param timeout_ms Timeout in milliseconds
 *                   Wait timeout in milliseconds
 * @param out_result Return parsed result
 *                   Output parsed result
 * @param out_result_length Return length of parsed result
 *                          Output length of the parsed result
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                   ESP_OK on success; an error code on failure
 */
esp_err_t data_wait_for_result_by_seq(uint16_t seq, int timeout_ms, void **out_result, size_t *out_result_length) {
    // Validate input parameters
    // Validate input arguments
    if (!out_result || !out_result_length) {
        ESP_LOGE(TAG, "out_result or out_result_length is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get start time and calculate timeout ticks
    // Record the start time and calculate the timeout in ticks
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // Take mutex for thread safety
        // Acquire the mutex for thread safety
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // Try to find entry
        // Try to find the entry
        entry_t *entry = find_entry_by_seq(seq);

        if (entry) {
            // Increase reference count to prevent release during waiting
            // Increment the reference count to prevent release while waiting
            xSemaphoreGive(s_map_mutex);

            // Wait for semaphore to be released
            // Wait for the semaphore
            if (xSemaphoreTake(entry->sem, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for seq=0x%04X timed out", seq);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_TIMEOUT;
            }

            // Get parsing result
            // Retrieve the parsed result
            if (entry->parse_result) {
                // Allocate new memory for out_result
                // Allocate new memory for out_result
                *out_result = malloc(entry->parse_result_length);
                if (*out_result == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for out_result");
                    if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        free_entry(entry);
                        xSemaphoreGive(s_map_mutex);
                    }
                    return ESP_ERR_NO_MEM;
                }

                // Copy entry->parse_result data to out_result
                // Copy entry->parse_result into out_result
                memcpy(*out_result, entry->parse_result, entry->parse_result_length);
                *out_result_length = entry->parse_result_length;  // Set length
                                                                 // Set the length
            } else {
                ESP_LOGE(TAG, "Parse result is NULL for seq=0x%04X", seq);
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_NOT_FOUND;
            }

            // Free entry
            // Release the entry
            if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
            }

            return ESP_OK;
        }

        // Check for timeout if entry not found
        // If the entry was not found, check for a timeout
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for seq=0x%04X, no entry found", seq);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT;
        }

        // Entry not found, release lock and wait before retry
        // If no entry was found, release the lock and wait before retrying
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before retry
                                       // Retry after 10 milliseconds
    }
}

/**
 * @brief Wait for parsing result by command set and ID, and return sequence number
 *        Wait for a cmd_set/cmd_id result and return its sequence number
 * 
 * Wait for parsing result of a specific command set and ID, and return its corresponding sequence number.
 * Wait for the specified command result and return the corresponding sequence number
 * 
 * @param cmd_set Command set
 *                Command set
 * @param cmd_id Command ID
 *               Command ID
 * @param timeout_ms Timeout in milliseconds
 *                   Wait timeout in milliseconds
 * @param out_seq Return sequence number
 *                Output sequence number
 * @param out_result Return parsed result
 *                   Output parsed result
 * @param out_result_length Return length of parsed result
 *                          Output length of the parsed result
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 *                   ESP_OK on success; an error code on failure
 */
esp_err_t data_wait_for_result_by_cmd(uint8_t cmd_set, uint8_t cmd_id, int timeout_ms, uint16_t *out_seq, void **out_result, size_t *out_result_length) {
    // Validate input parameters
    // Validate input arguments
    if (!out_result || !out_seq || !out_result_length) {
        ESP_LOGE(TAG, "out_result, out_seq or out_result_length is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get start time and calculate timeout ticks
    // Record the start time and calculate the timeout in ticks
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // Take mutex for thread safety
        // Acquire the mutex for thread safety
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take mutex");
            return ESP_ERR_INVALID_STATE;
        }

        // Try to find entry
        // Try to find the entry
        entry_t *entry = find_entry_by_cmd_id(cmd_set, cmd_id);

        if (entry) {
            // Check if entry already has result
            // Check whether the entry already has a result
            if (entry->parse_result != NULL) {
                // Entry already has result, get it immediately
                // Retrieve an available result immediately
                *out_result = malloc(entry->parse_result_length);
                if (*out_result == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for out_result");
                    xSemaphoreGive(s_map_mutex);
                    return ESP_ERR_NO_MEM;
                }
                
                // Copy entry->parse_result data to out_result
                // Copy entry->parse_result into out_result
                memcpy(*out_result, entry->parse_result, entry->parse_result_length);
                *out_result_length = entry->parse_result_length;
                *out_seq = entry->seq;
                
                // Free entry
                // Release the entry
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
                return ESP_OK;
            }
            
            // Entry exists but no result yet, need to wait
            // The entry exists but its result is not ready; wait
            SemaphoreHandle_t sem_to_wait = entry->sem;
            xSemaphoreGive(s_map_mutex);
            
            // Wait for semaphore to be released
            // Wait for the semaphore
            if (xSemaphoreTake(sem_to_wait, timeout_ticks) != pdTRUE) {
                ESP_LOGW(TAG, "Wait for cmd_set=0x%04X cmd_id=0x%04X timed out", cmd_set, cmd_id);
                // Try to clean up the entry if it still exists
                // Try to clean up the entry if it still exists
                if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    entry_t *timeout_entry = find_entry_by_cmd_id(cmd_set, cmd_id);
                    if (timeout_entry) {
                        free_entry(timeout_entry);
                    }
                    xSemaphoreGive(s_map_mutex);
                }
                return ESP_ERR_TIMEOUT;
            }
            
            // Re-acquire mutex to get the result
            // Reacquire the mutex to retrieve the result
            if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to take mutex after semaphore wait");
                return ESP_ERR_INVALID_STATE;
            }
            
            // Find entry again after waiting
            // Look up the entry again after waiting
            entry = find_entry_by_cmd_id(cmd_set, cmd_id);
            if (!entry) {
                ESP_LOGE(TAG, "Entry not found after semaphore wait");
                xSemaphoreGive(s_map_mutex);
                return ESP_ERR_NOT_FOUND;
            }
            
            // Get parsing result
            // Retrieve the parsed result
            if (entry->parse_result) {
                // Allocate new memory for out_result
                // Allocate new memory for out_result
                *out_result = malloc(entry->parse_result_length);
                if (*out_result == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for out_result");
                    free_entry(entry);
                    xSemaphoreGive(s_map_mutex);
                    return ESP_ERR_NO_MEM;
                }
                // Copy entry->parse_result data to out_result
                // Copy entry->parse_result into out_result
                memcpy(*out_result, entry->parse_result, entry->parse_result_length);
                *out_result_length = entry->parse_result_length;
            } else {
                ESP_LOGE(TAG, "Parse result is NULL for cmd_set=0x%04X cmd_id=0x%04X", cmd_set, cmd_id);
                free_entry(entry);
                xSemaphoreGive(s_map_mutex);
                return ESP_ERR_NOT_FOUND;
            }

            // Save sequence number
            // Save the sequence number
            *out_seq = entry->seq;

            // Free entry
            // Release the entry
            free_entry(entry);
            xSemaphoreGive(s_map_mutex);

            return ESP_OK;
        }

        // Check for timeout if entry not found
        // If the entry was not found, check for a timeout
        TickType_t elapsed_time = xTaskGetTickCount() - start_time;
        if (elapsed_time >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout while waiting for cmd_set=0x%04X cmd_id=0x%04X, no entry found", cmd_set, cmd_id);
            xSemaphoreGive(s_map_mutex);
            return ESP_ERR_TIMEOUT;
        }

        // Entry not found, release lock and wait before retry
        // If no entry was found, release the lock and wait before retrying
        xSemaphoreGive(s_map_mutex);
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before retry
                                       // Retry after 10 milliseconds
    }
}

/**
 * @brief Register camera status update callback
 *        Register the camera state update callback
 * 
 * This function registers a callback function for camera status updates. After registration,
 * the callback function will be called to synchronize the latest camera status when specific notifications are received.
 * Register a callback invoked by relevant notifications to synchronize camera state
 * 
 * @param callback Callback function pointer, pointing to user-defined callback function
 *                 Pointer to the user-defined callback
 */
static camera_status_update_cb_t status_update_callback = NULL;
void data_register_status_update_callback(camera_status_update_cb_t callback) {
    status_update_callback = callback;
}

static new_camera_status_update_cb_t new_status_update_callback = NULL;
void data_register_new_status_update_callback(new_camera_status_update_cb_t callback) {
    new_status_update_callback = callback;
}

/**
 * @brief Task for processing notification data
 *        Notification processing task
 * 
 * This task runs in task context and processes notification data from the queue
 * Process notifications from the queue in task context
 * 
 * @param pvParameters Task parameters (unused)
 *                    Task argument (unused)
 */
static void notify_processing_task(void *pvParameters) {
    notify_data_t notify_data;
    
    while (1) {
        // Wait for notification data from queue
        // Wait for notification data from the queue
        if (xQueueReceive(notify_queue, &notify_data, portMAX_DELAY) == pdTRUE) {
            // Process the notification data
            // Process notification data
            process_notification_data(notify_data.data, notify_data.data_length);
            
            // Free the allocated data
            // Free the allocated data
            free(notify_data.data);
        }
    }
}

/**
 * @brief Process notification data (moved from interrupt context to task context)
 *        Process notifications in task context instead of interrupt context
 * 
 * This function contains the original logic from receive_camera_notify_handler
 * Contains the original receive_camera_notify_handler logic
 * 
 * @param raw_data Raw notification data
 *                 Raw notification data
 * @param raw_data_length Data length
 *                        Data length
 */
static void process_notification_data(const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // Validate input arguments
    if (!raw_data || raw_data_length < 2) {
        ESP_LOGW(TAG, "Notify data is too short or null, skip parse");
        return;
    }

    // Check frame header
    // Check the frame header
    if (raw_data[0] == 0xAA || raw_data[0] == 0xaa) {
        ESP_LOGI(TAG, "Notification received, attempting to parse...");

        // ESP_LOG_BUFFER_HEX(TAG, raw_data, raw_data_length);  // Print notification content
                                                                // Log the notification contents

        // Print notification content in pink color
        // Print the notification contents in pink
        printf("\033[95m");
        printf("RX: [");
        for (size_t i = 0; i < raw_data_length; i++) {
            printf("%02X", raw_data[i]);
            if (i < raw_data_length - 1) {
                printf(", ");
            }
        }
        printf("] (%zu bytes)\n", raw_data_length);
        printf("\033[0m");
        printf("\033[0;32m");
                                                             
        // Define parsing result structure
        // Declare the parsed result structure
        protocol_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        // Call protocol_parse_notification to parse notification frame
        // Parse the notification frame with protocol_parse_notification
        int ret = protocol_parse_notification(raw_data, raw_data_length, &frame);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to parse notification frame, error: %d", ret);
            return;
        }

        // Parse data segment
        // Parse the data segment
        void *parse_result = NULL;
        size_t parse_result_length = 0;
        if (frame.data && frame.data_length > 0) {
            // Assume protocol_parse_data returns void* type
            // Assume protocol_parse_data returns void*
            parse_result = protocol_parse_data(frame.data, frame.data_length, frame.cmd_type, &parse_result_length);
            if (parse_result == NULL) {
                ESP_LOGE(TAG, "Failed to parse data segment, parse_result is null");
                return;
            } else {
                ESP_LOGI(TAG, "Data segment parsed successfully");
            }
        } else {
            ESP_LOGW(TAG, "Data segment is empty, skipping data parsing");
            return;
        }

        // Get actual seq (assuming frame has seq field)
        // Get the actual sequence number from the frame's seq field
        uint16_t actual_seq = frame.seq;
        uint8_t actual_cmd_set = frame.data[0];
        uint8_t actual_cmd_id = frame.data[1];
        ESP_LOGI(TAG, "Parsed seq = 0x%04X, cmd_set=0x%04X, cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);

        // Find corresponding entry
        // Find the corresponding entry
        if (xSemaphoreTake(s_map_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            entry_t *entry = find_entry_by_seq(actual_seq);
            if (entry) {
                // Assume parse_result is void* object returned by protocol_parse_data
                // Assume parse_result is the void* returned by protocol_parse_data
                if (parse_result != NULL) {
                    // Put parsing result into corresponding entry
                    // Store the parsed result in the corresponding entry
                    entry->parse_result = parse_result;  // Store void* result in entry's value field
                                                         // Store the void* result in the entry's value field
                    entry->parse_result_length = parse_result_length; // Record result length
                                                                      // Record the result length
                    // Wake up waiting task
                    // Wake the waiting task
                    xSemaphoreGive(entry->sem);
                } else {
                    ESP_LOGE(TAG, "Parsing data failed, entry not updated");
                }
            } else {
                // Camera actively pushed notification
                // Unsolicited camera notification
                ESP_LOGW(TAG, "No waiting entry found for seq=0x%04X, creating a new entry by cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                // Allocate a new entry
                // Allocate a new entry
                entry = allocate_entry_by_cmd(actual_cmd_set, actual_cmd_id);
                if (entry == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate entry for seq=0x%04X cmd_set=0x%04X cmd_id=0x%04X", actual_seq, actual_cmd_set, actual_cmd_id);
                } else {
                    // Initialize parsing result
                    // Initialize the parsed result
                    entry->parse_result = parse_result;
                    entry->parse_result_length = parse_result_length;
                    entry->seq = actual_seq;
                    entry->last_access_time = xTaskGetTickCount();
                    ESP_LOGI(TAG, "New entry allocated for seq=0x%04X", actual_seq);
                    // Wake up any waiting tasks
                    // Wake any waiting tasks
                    xSemaphoreGive(entry->sem);
                }
            }
            xSemaphoreGive(s_map_mutex);
        }

        // Handle camera actively pushed status
        // Handle unsolicited camera state updates
        if (actual_cmd_set == 0x1D && actual_cmd_id == 0x02 && status_update_callback) {
            // Create new memory copy for status update callback
            // Create a separate memory copy for the state callback
            void *status_copy = NULL;
            if (parse_result != NULL && parse_result_length > 0) {
                status_copy = malloc(parse_result_length);
                if (status_copy != NULL) {
                    memcpy(status_copy, parse_result, parse_result_length);
                    status_update_callback(status_copy);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for status update callback");
                }
            }
        }

        // Handle new camera actively pushed status
        // Handle the newer camera state update format
        if (actual_cmd_set == 0x1D && actual_cmd_id == 0x06 && new_status_update_callback) {
            // Create new memory copy for new status update callback
            // Create a separate memory copy for the new state callback
            void *new_status_copy = NULL;
            if (parse_result != NULL && parse_result_length > 0) {
                new_status_copy = malloc(parse_result_length);
                if (new_status_copy != NULL) {
                    memcpy(new_status_copy, parse_result, parse_result_length);
                    new_status_update_callback(new_status_copy);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for new status update callback");
                }
            }
        }
    } else {
        // ESP_LOGW(TAG, "Received frame does not start with 0xAA, ignoring...");
    }
}

/**
 * @brief Handle camera notifications and parse data (callback function)
 *        Camera notification parsing callback
 * 
 * This function is called from BLE interrupt context and queues the data for processing
 * Called from BLE interrupt context; queues data for later processing
 * 
 * @param raw_data Raw notification data
 *                 Raw notification data
 * @param raw_data_length Data length
 *                        Data length
 */
void receive_camera_notify_handler(const uint8_t *raw_data, size_t raw_data_length) {
    // Validate input parameters
    // Validate input arguments
    if (!raw_data || raw_data_length < 2) {
        ESP_LOGW(TAG, "Notify data is too short or null, skip parse");
        return;
    }

    // Allocate memory for the data
    // Allocate memory for the data
    uint8_t *data_copy = malloc(raw_data_length);
    if (data_copy == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for notification data");
        return;
    }

    // Copy the data
    // Copy the data
    memcpy(data_copy, raw_data, raw_data_length);

    // Prepare notification data structure
    // Prepare the notification structure
    notify_data_t notify_data = {
        .data = data_copy,
        .data_length = raw_data_length
    };

    // Send to queue for processing in task context
    // Enqueue for processing in task context
    if (xQueueSend(notify_queue, &notify_data, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue notification data");
        free(data_copy);
    }
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
esp_err_t data_send_raw_bytes(const char *raw_data_string, int timeout_ms) {
    ESP_LOGI(TAG, "%s: Sending raw bytes: %s", __FUNCTION__, raw_data_string);
    
    if (raw_data_string == NULL) {
        ESP_LOGE(TAG, "Invalid input: raw_data_string is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Parse the input string to extract bytes
    // Parse the input string into bytes
    size_t max_bytes = strlen(raw_data_string) / 2; // Estimate max possible bytes
    uint8_t *raw_bytes = malloc(max_bytes);
    if (raw_bytes == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for raw bytes");
        return ESP_ERR_NO_MEM;
    }

    size_t byte_count = 0;
    const char *ptr = raw_data_string;
    
    while (*ptr && byte_count < max_bytes) {
        // Skip whitespace, commas, hyphens, colons
        // Skip spaces, commas, hyphens, and colons
        while (*ptr && (*ptr == ' ' || *ptr == ',' || *ptr == '-' || *ptr == ':')) {
            ptr++;
        }
        
        if (!*ptr) break;
        
        // Parse hex byte (support both uppercase and lowercase)
        // Parse hexadecimal bytes, accepting upper and lower case
        char hex_str[3] = {0};
        if (isxdigit((unsigned char)*ptr)) {
            hex_str[0] = tolower(*ptr++);
            if (*ptr && isxdigit((unsigned char)*ptr)) {
                hex_str[1] = tolower(*ptr++);
            } else {
                // Single digit, pad with 0
                // Pad a single digit with zero
                hex_str[1] = '0';
                memmove(hex_str + 1, hex_str, 2);
                hex_str[0] = '0';
            }
            
            // Convert hex string to byte
            // Convert the hexadecimal string to bytes
            char *endptr;
            unsigned long value = strtoul(hex_str, &endptr, 16);
            if (endptr == hex_str + 2) {
                raw_bytes[byte_count++] = (uint8_t)value;
            }
        } else {
            ptr++; // Skip invalid character
        }
    }

    if (byte_count == 0) {
        ESP_LOGE(TAG, "No valid bytes found in input string");
        free(raw_bytes);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Parsed %zu bytes from input string", byte_count);

    // Print parsed bytes for debugging
    // Log the parsed bytes for debugging
    printf("\033[96m");  // Select cyan output
    printf("Raw TX: [");
    for (size_t i = 0; i < byte_count; i++) {
        printf("%02X", raw_bytes[i]);
        if (i < byte_count - 1) {
            printf(", ");
        }
    }
    printf("] (%zu bytes)\n", byte_count);
    printf("\033[0m");

    // Send the raw bytes directly using a dummy sequence number
    // Send raw bytes with a dummy sequence number
    uint16_t dummy_seq = 0xFFFF;
    esp_err_t ret = data_write_without_response(dummy_seq, raw_bytes, byte_count);
    
    free(raw_bytes);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send raw bytes, error: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Raw bytes sent successfully");
    return ESP_OK;
}
