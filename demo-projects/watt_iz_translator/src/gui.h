/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <lvgl.h>
#include "esp_heap_caps.h"                // needed for PSRAM alloc
#include "utils.h"
#include "gdriver.h"
#include "speechServices.h"


enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_START_REC,
   GUI_EVENT_STOP_REC,
};

enum {
   ROLLER_FROM=0,
   ROLLER_TO
};

// GUI event struct
typedef struct {
   uint8_t event_code;
} gui_event_t ;


typedef struct {
   char language_name[20];                // ex: "English", "French", "German", etc.
   char lang_code[10];                    // google lang code. Ex: "en-US"
   char voice_name[30];                   // voice name, ex: "en-US-Wavenet-G" 
   const lv_font_t *lang_font;
   float speakingRate;                    // 0.0 - 1.0
} language_info_t ;
   
// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

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
void arc_event_cb(lv_event_t * e);
int16_t getGUIevents(void);
void setXFromText(char *labtext, uint8_t lang_sel_from); 
void setXToText(char *labtext, uint8_t lang_sel_to);
void setRecProgress(uint8_t progress);
void setSpeakButnText(const char *butn_text);
void roller_event_cb(lv_event_t * e);
// language_info_t *language_lookup(char *lang);     // ex: "English" -> "en-US" + google voice name
uint8_t getLangIndex(uint8_t roller_id);
// char *getXlateToken(uint8_t roller);
void lv_force_refresh(lv_obj_t *_obj);

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

static lv_obj_t *translate_butn;
static lv_obj_t *speak_text_label;
static lv_obj_t *scrn2_wifi_label;
static lv_obj_t *translate_butn_label;
static lv_obj_t *progress_arc;
static lv_obj_t *arc_label;
static lv_obj_t *repeat_trans_butn;

static lv_timer_t *scrn1_timer;
static lv_timer_t *talk_timer;

static lv_obj_t *translate_from_label;
static lv_obj_t *translate_to_label;
static lv_obj_t *xfrom_roller;
static lv_obj_t *xto_roller;
static lv_obj_t *lang_swap_butn;

static lv_obj_t *xfrom_hdr_label;
static lv_obj_t *xto_hdr_label;


extern language_info_t LanguageArray[];