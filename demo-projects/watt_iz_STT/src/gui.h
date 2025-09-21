/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <TFT_eSPI.h>                     // LCD hardware drive
#include <lvgl.h>
#include "esp_heap_caps.h"                // needed for PSRAM alloc
#include "utils.h"
// #include "text_to_speech_icon_300x150_bw_inverted.c"

// Touchpoint struct
typedef struct {
   int16_t x;
   int16_t y;
} touch_point_t ;

enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_START_REC,
   GUI_EVENT_STOP_REC,
};

// GUI event struct
typedef struct {
   uint8_t event_code;

} gui_event_t ;


// Capacitive touch I2C address
#define FT6336_ADDR             0x38      

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits
#define COLOR_BUFFER_SIZE  (SCREEN_WIDTH * 120)   // number of display lines in buffer

// Function Prototypes
uint8_t readFT6336(touch_point_t* points, uint8_t maxPoints, uint8_t scrn_rotate);
bool guiInit(void);
void setBacklight(uint8_t bri);
void demoBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
void arc_event_cb(lv_event_t * e);
int16_t getGUIevents(void);
void setLabelText(char *labtext);
void setRecProgress(uint8_t progress);
void setSpeakButnText(const char *butn_text);

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
static lv_obj_t *scrn1;
static lv_obj_t *scrn2;

static lv_obj_t *speak_butn;
static lv_obj_t *speak_text_label;
static lv_obj_t *scrn1_wifi_label;
static lv_obj_t *scrn2_wifi_label;

static lv_obj_t *speak_butn_label;
static lv_obj_t *progress_arc;

static lv_timer_t *scrn1_timer;
static lv_timer_t *talk_timer;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif






