#pragma once
#include <assert.h>
/* SDK packing is irrelevant to these typed callbacks; wire parsing is not mocked here. */
#ifdef _MSC_VER
#define __attribute__(x)
#pragma warning(push)
#pragma warning(disable:4200) /* Upstream SDK has a flexible zero-length array. */
#endif
#include "dji_protocol_data_structures.h"
#include "enums_logic.h"
#ifdef _MSC_VER
#undef __attribute__
#pragma warning(pop)
#endif
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_MAC_BT 0
#define ESP_ERROR_CHECK(result) assert((result) == 0)
#define PROTOCOL_CONNECTED 1
TickType_t xTaskGetTickCount(void);
int esp_read_mac(uint8_t *mac, int kind);
void data_init(void);
void data_register_status_update_callback(void (*callback)(void *));
void data_register_new_status_update_callback(void (*callback)(void *));
void update_camera_state_handler(void *data);
void update_new_camera_state_handler(void *data);
int connect_logic_protocol_connect(uint32_t id, size_t size, int8_t *mac,
                                   int a, int b, uint16_t code, int c);
int subscript_camera_status(int mode, int frequency);
int connect_logic_get_state(void);
bool is_camera_recording(void);
extern bool camera_status_initialized;
version_query_response_frame_t *command_logic_get_version(void);
record_control_response_frame_t *command_logic_start_record(void);
record_control_response_frame_t *command_logic_stop_record(void);
