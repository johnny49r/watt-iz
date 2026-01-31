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

// Function prototypes
void printDateTime(const RtcDateTime& dt);

// Firmware upgrade path/filename
#define FIRMWARE_UPGRADE_FILENAME   "/update/firmware.bin"

// Function prototypes
bool fw_update_from_sd_wrapper(const char* path);





