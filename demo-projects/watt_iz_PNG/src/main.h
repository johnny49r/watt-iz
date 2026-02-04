/**
 * 
 */
#pragma once

#include <Arduino.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include "config.h"
#include "gui.h"
#include "sd_lvgl_fs.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

// Firmware upgrade path/filename
#define FIRMWARE_UPGRADE_FILENAME   "/update/firmware.bin"

// // Capacitive touch 
// #define FT6336_ADDR                 0x38
// struct TouchPoint {
//     int16_t x;
//     int16_t y;
// };

// // function prototypes
// uint8_t readFT6336(TouchPoint* points, uint8_t maxPoints, uint8_t scrn_rotate);


// Prototypes
bool fw_update_from_sd_wrapper(const char* path);


