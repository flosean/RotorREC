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

#include "enums_logic.h"

const char* camera_mode_to_string(camera_mode_t mode) {
    switch (mode) {
        case CAMERA_MODE_SLOW_MOTION:
            return "Slow Motion";
        case CAMERA_MODE_NORMAL:
            return "Video";
        case CAMERA_MODE_TIMELAPSE:
            return "Timelapse";
        case CAMERA_MODE_PHOTO:
            return "Photo";
        case CAMERA_MODE_HYPERLAPSE:
            return "Hyperlapse";
        case CAMERA_MODE_LIVE_STREAMING:
            return "Live Streaming";
        case CAMERA_MODE_UVC_STREAMING:
            return "UVC Live Streaming";
        case CAMERA_MODE_SUPERNIGHT:
            return "SuperNight";
        case CAMERA_MODE_SUBJECT_TRACKING:
            return "Subject Tracking";

        case CAMERA_MODE_PANORAMIC_VIDEO_360:
            return "Panoramic Video (Osmo360)";
        case CAMERA_MODE_HYPERLAPSE_360:
            return "Hyperlapse (Osmo360)";
        case CAMERA_MODE_SELFIE_360:
            return "Selfie Mode (Osmo360)";
        case CAMERA_MODE_PANORAMIC_PHOTO_360:
            return "Panoramic Photo (Osmo360)";
        case CAMERA_MODE_BOOST_VIDEO_360:
            return "Boost Video (Osmo360)";
        case CAMERA_MODE_VORTEX_360:
            return "Vortex (Osmo360)";
        case CAMERA_MODE_PANORAMIC_SUPERNIGHT_360:
            return "360-degree SuperNight (Osmo360)";
        case CAMERA_MODE_SINGLE_LENS_SUPERNIGHT_360:
            return "Single Lens SuperNight (Osmo360)";

        default:
            return "Unknown mode";
    }
}

const char* camera_status_to_string(camera_status_t status) {
    switch (status) {
        case CAMERA_STATUS_SCREEN_OFF:
            return "Screen off";
        case CAMERA_STATUS_LIVE_STREAMING:
            return "Live streaming (including screen-on without recording)";
        case CAMERA_STATUS_PLAYBACK:
            return "Playback";
        case CAMERA_STATUS_PHOTO_OR_RECORDING:
            return "Photo or recording";
        case CAMERA_STATUS_PRE_RECORDING:
            return "Pre-recording";
        default:
            return "Unknown status";
    }
}

const char* video_resolution_to_string(video_resolution_t res) {
    switch (res) {
        case VIDEO_RESOLUTION_1080P: return "1920x1080P";
        case VIDEO_RESOLUTION_4K_16_9: return "4096x2160P 4K 16:9";
        case VIDEO_RESOLUTION_2K_16_9: return "2720x1530P 2.7K 16:9";
        case VIDEO_RESOLUTION_1080P_9_16: return "1920x1080P 9:16";
        case VIDEO_RESOLUTION_2K_9_16: return "2720x1530P 9:16";
        case VIDEO_RESOLUTION_2K_4_3: return "2720x2040P 2.7K 4:3";
        case VIDEO_RESOLUTION_4K_4_3: return "4096x3072P 4K 4:3";
        case VIDEO_RESOLUTION_4K_9_16: return "4096x2160P 4K 9:16";
        case VIDEO_RESOLUTION_L: return "Ultra Wide 30MP (Osmo360)";
        case VIDEO_RESOLUTION_M: return "Wide 20MP (Osmo360)";
        case VIDEO_RESOLUTION_S: return "Standard 12MP (Osmo360)";
        default: return "Unknown resolution";
    }
}

const char* fps_idx_to_string(fps_idx_t fps) {
    switch (fps) {
        case FPS_24: return "24fps";
        case FPS_25: return "25fps";
        case FPS_30: return "30fps";
        case FPS_48: return "48fps";
        case FPS_50: return "50fps";
        case FPS_60: return "60fps";
        case FPS_100: return "100fps";
        case FPS_120: return "120fps";
        case FPS_200: return "200fps";
        case FPS_240: return "240fps";
        default: return "Unknown FPS";
    }
}

const char* eis_mode_to_string(eis_mode_t mode) {
    switch (mode) {
        case EIS_MODE_OFF: return "Off";
        case EIS_MODE_RS: return "RS";
        case EIS_MODE_RS_PLUS: return "RS+";
        case EIS_MODE_HB: return "HB";
        case EIS_MODE_HS: return "HS";
        default: return "Unknown EIS mode";
    }
}
