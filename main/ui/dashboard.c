#include "dashboard.h"

#include <stdio.h>

#include "lvgl.h"

static lv_obj_t *s_connection_label;
static lv_obj_t *s_record_label;
static lv_obj_t *s_spec_label;
static lv_obj_t *s_detail_label;

static lv_obj_t *create_label(lv_obj_t *parent, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, 304);
    lv_obj_set_pos(label, 8, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void format_duration(uint32_t seconds, char *output, size_t output_size)
{
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    if (hours > 0) {
        snprintf(output, output_size, "%lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes, (unsigned long)secs);
    } else {
        snprintf(output, output_size, "%02lu:%02lu",
                 (unsigned long)minutes, (unsigned long)secs);
    }
}

void dashboard_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071018), 0);

    lv_obj_t *title = create_label(screen, 6, &lv_font_montserrat_14, 0x7ED8FF);
    lv_label_set_text(title, "RotorREC / CAMERA CONTROL");
    s_connection_label = create_label(screen, 28, &lv_font_montserrat_20, 0xFFCC66);
    s_record_label = create_label(screen, 58, &lv_font_montserrat_20, 0xF2F5F7);
    s_spec_label = create_label(screen, 88, &lv_font_montserrat_14, 0xA9BAC5);
    s_detail_label = create_label(screen, 111, &lv_font_montserrat_14, 0xA9BAC5);

    lv_obj_t *help = create_label(screen, 149, &lv_font_montserrat_14, 0x6E8795);
    lv_label_set_text(help, "BOOT: REC   HOLD: CONNECT");
}

void dashboard_refresh(const camera_controller_state_t *state)
{
    if (state == NULL) {
        return;
    }
    uint32_t connection_color = 0xFFCC66;
    switch (state->phase) {
        case CAMERA_PHASE_RECONNECTING:
            lv_label_set_text(s_connection_label, "CAMERA RECONNECTING...");
            break;
        case CAMERA_PHASE_SCANNING:
            lv_label_set_text(s_connection_label, "SCANNING CAMERA...");
            break;
        case CAMERA_PHASE_VERIFYING:
            if (state->protocol == CAMERA_PROTOCOL_GOPRO)
                lv_label_set_text(s_connection_label, "GOPRO BLE PAIRING...");
            else
                lv_label_set_text_fmt(s_connection_label, "CONFIRM CODE  %04u", state->pairing_code);
            break;
        case CAMERA_PHASE_ACTION2_SESSION:
            lv_label_set_text(s_connection_label, "ACTION 2 SESSION SETUP");
            connection_color = 0x57E389;
            break;
        case CAMERA_PHASE_ACTION2_PAIRING:
            lv_label_set_text(s_connection_label, "WAITING ACTION 2 PAIRING");
            connection_color = 0x57E389;
            break;
        case CAMERA_PHASE_READY:
            if (state->protocol == CAMERA_PROTOCOL_GOPRO) {
                lv_label_set_text(s_connection_label, "GOPRO CONNECTED");
            } else if (state->protocol == CAMERA_PROTOCOL_DJI_ACTION2) {
                lv_label_set_text(s_connection_label, "ACTION 2 BLE READY");
            } else {
                lv_label_set_text(s_connection_label, state->snapshot.valid
                    ? "DJI CONNECTED" : "CONNECTED - WAITING STATUS");
            }
            connection_color = 0x57E389;
            break;
        case CAMERA_PHASE_SCAN_FAILED:
            lv_label_set_text(s_connection_label, "NO CAMERA FOUND");
            connection_color = 0xFF6B6B;
            break;
        case CAMERA_PHASE_GATT_MISMATCH:
            lv_label_set_text(s_connection_label, "BLE OK / GATT MISMATCH");
            connection_color = 0xFF6B6B;
            break;
        case CAMERA_PHASE_PAIR_REJECTED:
            lv_label_set_text(s_connection_label, "PAIRING REJECTED / TIMEOUT");
            connection_color = 0xFF6B6B;
            break;
        case CAMERA_PHASE_SUBSCRIBE_FAILED:
            lv_label_set_text(s_connection_label, "STATUS SUBSCRIBE FAILED");
            connection_color = 0xFF6B6B;
            break;
        case CAMERA_PHASE_INITIALIZING:
            lv_label_set_text(s_connection_label, "BLE INITIALIZING...");
            break;
        case CAMERA_PHASE_WAITING_STATUS:
            lv_label_set_text(s_connection_label, "WAITING CAMERA STATUS...");
            break;
        default:
            lv_label_set_text(s_connection_label, "HOLD BOOT TO PAIR");
            break;
    }
    lv_obj_set_style_text_color(s_connection_label, lv_color_hex(connection_color), 0);

    if (state->protocol == CAMERA_PROTOCOL_DJI_ACTION2 || state->protocol == CAMERA_PROTOCOL_GOPRO) {
        if (state->phase == CAMERA_PHASE_READY) {
            lv_label_set_text(s_record_label, state->command_pending
                ? "WAITING CAMERA..." : !state->recording_valid ? "STATUS UNKNOWN"
                : state->saving ? "CAMERA BUSY"
                : state->recording ? "REC - CAMERA STATUS" : "STANDBY - PRESS BOOT");
            lv_obj_set_style_text_color(s_record_label, lv_color_hex(
                state->command_pending || !state->recording_valid || state->saving
                ? 0xFFCC66 : state->recording ? 0xFF4D5A : 0xF2F5F7), 0);
        } else {
            lv_label_set_text(s_record_label, "--  NOT READY  --");
        }
        if (state->battery_valid) {
            lv_label_set_text_fmt(s_spec_label, "BAT %u%%   RSSI %d", state->snapshot.battery, state->rssi);
        } else {
            lv_label_set_text_fmt(s_spec_label, "BAT --   RSSI %d", state->rssi);
        }
        lv_label_set_text(s_detail_label, state->last_command_result == -3
            ? "NO COMMAND ACK - CHECK CAMERA"
            : state->protocol == CAMERA_PROTOCOL_GOPRO && !state->recording_valid
            ? "SELECT VIDEO / CHECK STATUS" : "HOLD BOOT TO PAIR");
        return;
    }

    if (!state->snapshot.valid) {
        lv_label_set_text(s_record_label, state->last_command_result == -1
            ? "CONNECT CAMERA FIRST" : "--  NOT READY  --");
        lv_label_set_text(s_spec_label, "WAITING FOR CAMERA STATUS");
        lv_label_set_text(s_detail_label, "DJI PUBLIC R SDK");
        return;
    }

    char elapsed[16];
    char remaining[16];
    format_duration(state->snapshot.record_time, elapsed, sizeof(elapsed));
    format_duration(state->snapshot.remain_time, remaining, sizeof(remaining));
    if (state->snapshot.recording) {
        lv_label_set_text_fmt(s_record_label, "REC  %s", elapsed);
    } else {
        lv_label_set_text_fmt(s_record_label, "%s  STANDBY", state->snapshot.mode);
    }
    lv_obj_set_style_text_color(s_record_label,
                                lv_color_hex(state->snapshot.recording ? 0xFF4D5A : 0xF2F5F7), 0);
    lv_label_set_text_fmt(s_spec_label, "%s  %s",
                          state->snapshot.mode, state->snapshot.parameters);
    lv_label_set_text_fmt(s_detail_label, "BAT %u%%   SD %luMB   REM %s",
                          state->snapshot.battery,
                          (unsigned long)state->snapshot.remain_capacity, remaining);
}
