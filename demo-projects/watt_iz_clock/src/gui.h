/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include "gdriver.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "utils.h"

// LCD backlight control params
#define BKLT_CHANNEL             0        // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ                600      // pwm period frequency in HZ
#define BKLT_RESOLUTION          8        // timer resolution - 8 bits

#define TFT_GREY                 0x5AEB

// Message Box key codes
enum {
   MBOX_BTN_NONE=0,
   MBOX_BTN_OK,
   MBOX_BTN_CANCEL,
};

// Function Prototypes
bool guiInit(void);
void setBacklight(uint8_t bri);
void demoBuilder(void);
void setDefaultStyles(void);

void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void demo_page3(lv_obj_t *parent);
void demo_page4(lv_obj_t *parent);
void refreshDateTime(const RtcDateTime& dt);
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag);
void closeMessageBox(void);
uint16_t getMessageBoxBtn(void);
static void calendar_event_handler(lv_event_t * e);
static void calendarPopup(bool use_chinese);

// Callbacks and event handlers
void butn_event_cb(lv_event_t * e);
static void calendar_btn_event_cb(lv_event_t * e);
void tileview_change_cb(lv_event_t *e);
void dd_event_handler_cb(lv_event_t * e);
void lv_spinbox_increment_event_cb(lv_event_t * e);
void lv_spinbox_decrement_event_cb(lv_event_t * e);
void mbox_event_cb(lv_event_t * e);

// lvgl graphics buffer lengths
#define PSRAM_LINES                 120   // PSRAM draw buffer 1/2 screen
#define SRAM_LINES                  12    // SRAM line buffer (for speed)

// PSRAM LVGL draw buffers (two for double-buffered partial rendering)
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

// lvgl objects
static lv_obj_t *tileview;
static lv_obj_t *tv_demo_page1;
static lv_obj_t *tv_demo_page2;
static lv_obj_t *tv_demo_page3;
static lv_obj_t *tv_demo_page4;

static lv_obj_t *next_butn;
static lv_obj_t *time_label;
static lv_obj_t *ampm_label;
static lv_obj_t *date_label;
static lv_obj_t *set_time_butn;
static lv_obj_t *temp_label;

static lv_obj_t *set_datetime_butn;
static lv_obj_t *cancel_datetime_butn;

static lv_obj_t *dd_month;
static lv_obj_t *dd_dayOfMonth;
static lv_obj_t *dd_year;
static lv_obj_t *dd_hour;
static lv_obj_t *dd_minute;
static lv_obj_t *dd_second;

static lv_obj_t *set_date_label;

static lv_obj_t *msgbox1;
static lv_obj_t *calendar_popup_cont;
static lv_obj_t *calendar_close_butn;
static lv_obj_t *clock_calendar_butn;
static lv_style_t style_calendar;
static lv_style_t style_home_butn_default;

static lv_obj_t *dd_timezone;
static lv_obj_t *auto_datetime_butn;


