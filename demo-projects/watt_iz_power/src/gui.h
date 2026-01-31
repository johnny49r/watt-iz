/**
 * @brief gui.h
 */
#pragma once

#include "config.h"

#include <lvgl.h>
#include "esp_heap_caps.h"
#include "utils.h"
#include "gdriver.h"

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

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
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag);
void closeMessageBox(void);    
uint16_t getMessageBoxBtn(void);  

void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
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
static lv_obj_t *scrn1;
static lv_obj_t *scrn2;

static lv_obj_t *pwr_chart;
static lv_chart_series_t *ser1;
static lv_chart_series_t *ser2;
static lv_timer_t *chart_timer;
static lv_obj_t *batv_label;
static lv_obj_t *ichg_label;
static lv_obj_t *soc_label;
static lv_obj_t *time_to_charge_label;
static lv_obj_t *chg_led;

static lv_obj_t *msgbox1;









