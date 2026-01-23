/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <lvgl.h>
#include "esp_heap_caps.h"                // needed for PSRAM alloc
#include "utils.h"
#include "gdriver.h"

enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_TALK,
};

// GUI event struct
typedef struct {
   uint8_t event_code;

} gui_event_t ;
 

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
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
int16_t getGUIevents(void);
void setLabelText(char *labtext);
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag);
void closeMessageBox(void);
void mbox_event_cb(lv_event_t * e);
uint16_t getMessageBoxBtn(void);      

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

static lv_obj_t *talk_butn;
static lv_obj_t *talk_text_label;
static lv_obj_t *scrn1_wifi_label;
static lv_obj_t *scrn2_wifi_label;

static lv_obj_t *next_butn;
static lv_obj_t *next_butn_label;

static lv_timer_t *scrn1_timer;
static lv_timer_t *talk_timer;

static lv_obj_t *msgbox1;



