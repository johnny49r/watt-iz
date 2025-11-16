/**
 * @brief main.h
 */
#pragma once

#include <Arduino.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include "config.h"
#include "gui.h"
#include "sd_lvgl_fs.h"
#include "speechServices.h"

enum {
    STATE_NONE=0,
    STATE_VOICE_RECORDING,
    STATE_SPEECH_TO_TEXT,
    STATE_CHAT_REQUEST,
    STATE_TRANSLATE_REQUEST,
};


// Function prototypes





