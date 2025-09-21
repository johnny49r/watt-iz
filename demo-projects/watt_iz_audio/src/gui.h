/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <TFT_eSPI.h>                     // LCD hardware drive
#include <lvgl.h>
#include "sdcard.h"
#include "esp_dsp.h"                      // for FFT using the esp32-s3 dsp functions
#include "esp_heap_caps.h"
#include "esp32s3_fft.h"
#include "audio.h"

// Touchpoint struct
typedef struct {
   int16_t x;
   int16_t y;
} touch_point_t ;


#define FT6336_ADDR             0x38      

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

#define COLOR_BUFFER_SIZE  (SCREEN_WIDTH * 120)   // number of display lines in buffer
#define TFT_GREY 0x5AEB

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
void butn_event_cb(lv_event_t * e);
void switch_event_handler(lv_event_t * e);
static void slider_event_cb(lv_event_t * e);

// lvgl display driver stuff
#define ROWS_PER_BUF       26   // LVGL draw buffer height (PSRAM)
#define LINES_PER_CHUNK    20  // internal bounce chunk (SRAM)
#define LV_BUFFER_SIZE (SCREEN_WIDTH * ROWS_PER_BUF)   // 26 lines of 320 horiz pixels
static lv_display_t *main_disp;              // LVGL display object

// Internal DMA-capable (or just SRAM-capable) line buffer
static uint16_t *dma_linebuf;
static lv_color_t *buf1;      // 1st display buffer
static lv_color_t *buf2;      // 2nd display buffer

// lvgl style objects
static lv_style_t style_tileview;
static lv_style_t style_butn_released;
static lv_style_t style_butn_pressed;
static lv_style_t style_label_default;

// lvgl objects
static lv_obj_t *tileview;
static lv_obj_t *tv_demo_page1;
static lv_obj_t *tv_demo_page2;
static lv_obj_t *tv_demo_page3;
static lv_obj_t *next_butn;
static lv_obj_t *scrn1;
static lv_obj_t *scrn2;
static lv_obj_t *chart;
static lv_obj_t *chart_sw;
static lv_chart_series_t *ser1;
static lv_chart_series_t *ser2;

static lv_timer_t *chart_timer;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif

static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;

static lv_obj_t *tone_slider;
static lv_obj_t *tone_slider_label;
static lv_obj_t *tone_vol_slider;
static lv_obj_t *tone_vol_slider_label;

// A flag to track latched state
static bool db1_latched = false;
static int16_t tone_volume;



