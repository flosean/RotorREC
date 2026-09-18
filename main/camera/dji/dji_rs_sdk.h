#pragma once

#include <stdint.h>

#include "camera_types.h"

typedef enum {
    DJI_RSDK_CONNECT_FAILED,
    DJI_RSDK_CONNECT_READY,
    DJI_RSDK_CONNECT_SUBSCRIBE_FAILED,
} dji_rs_sdk_connect_result_t;

void dji_rs_sdk_init(void);
void dji_rs_sdk_reset(void);
dji_rs_sdk_connect_result_t dji_rs_sdk_connect(uint16_t pairing_code);
int dji_rs_sdk_toggle_recording(void);
int dji_rs_sdk_set_recording(bool recording);
void dji_rs_sdk_get_snapshot(camera_snapshot_t *snapshot);
