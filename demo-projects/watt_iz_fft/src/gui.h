/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include "gdriver.h"
#include <lvgl.h>
#include "sd_lvgl_fs.h"
#include "esp_dsp.h"                      // for FFT using the esp32-s3 dsp functions
#include "esp_heap_caps.h"
#include "esp32s3_fft.h"
#include "audio.h"
#include <math.h>
#include <stdlib.h>
    

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

#define TFT_GREY 0x5AEB

// Message Box key codes
enum {
   MBOX_BTN_NONE=0,
   MBOX_BTN_OK,
   MBOX_BTN_CANCEL,
};

// Function Prototypes
uint8_t readFT6336(touch_point_t* points, uint8_t maxPoints, uint8_t scrn_rotate);
bool guiInit(void);
void setBacklight(uint8_t bri);
void demoBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void demo_page3(lv_obj_t *parent);
void demo_page4(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
void openMessageBox(const char *title, const char *body_text, const char *btn1_text, 
         uint32_t btn1_tag, const char *btn2_text, uint32_t bt2_tag,
         const char *btn3_text, uint32_t btn3_tag);
void closeMessageBox(void);
uint16_t getMessageBoxBtn(void);          // return msg box response
void mbox_event_cb(lv_event_t * e);
float ddGetCutoffFreq(void);
float ddGetQFactor(void);

void switch_event_handler(lv_event_t * e);
static void slider_event_cb(lv_event_t * e);
void dd_event_handler_cb(lv_event_t * e);

// lvgl display driver stuff
static lv_display_t *main_disp;              // LVGL display object
static uint16_t *lv_buf1_psram = nullptr;
static uint16_t *lv_buf2_psram = nullptr;
// DMA-capable SRAM bounce buffer (used inside flush)
static uint16_t *sram_linebuf = nullptr;

// lvgl style objects
static lv_style_t style_tileview;
static lv_style_t style_butn_released;
static lv_style_t style_butn_pressed;
static lv_style_t style_label_default;

static lv_obj_t *audio_cont;           // container for audio controls
static lv_obj_t *record_butn;
static lv_obj_t *play_butn;
static lv_obj_t *stop_butn;
static lv_obj_t *pause_butn;
static lv_obj_t *rec_chart;
static lv_chart_series_t *rec_ser;
static lv_obj_t *dd_freq;
static lv_obj_t *dd_qfactor;
static lv_timer_t *rec_chart_timer;

static lv_obj_t *progress_bar;

// lvgl objects
static lv_obj_t *tileview;
static lv_obj_t *tv_demo_page1;
static lv_obj_t *tv_demo_page2;
static lv_obj_t *tv_demo_page3;
static lv_obj_t *tv_demo_page4;
static lv_obj_t *fft_chart;
static lv_obj_t *chart_sw;
static lv_obj_t *start_scan_butn;
static lv_chart_series_t *ser1;
static lv_chart_series_t *ser2;

static lv_timer_t *chart_timer;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif

static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;

static lv_obj_t *tone_freq_slider;
static lv_obj_t *tone_freq_slider_label;
static lv_obj_t *tone_vol_slider;
static lv_obj_t *tone_vol_slider_label;
static lv_obj_t *pb_vol_slider;
static lv_obj_t *pb_vol_label;
static lv_obj_t *pb_progress_label;

// A flag to track latched state
static bool db1_latched = false;
static int16_t tone_volume;

// Messagebox objects
static lv_obj_t *msgbox1;

// lvgl graphics buffer lengths
#define PSRAM_LINES                 120   // PSRAM draw buffer 1/2 screen
#define SRAM_LINES                  12    // SRAM line buffer (for speed)

