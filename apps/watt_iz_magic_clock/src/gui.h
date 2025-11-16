/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include "gdriver.h"
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "utils.h"
#include "audio.h"
#include "speechServices.h"

typedef struct {
   char language_name[20];                // ex: "English", "French", "German", etc.
   char lang_code[10];                    // google lang code. Ex: "en-US"
   char voice_name[30];                   // voice name, ex: "en-US-Wavenet-G" 
   const lv_font_t *lang_font;
   float speakingRate;                    // 0.0 - 1.0
} language_info_t ;

#define ICON_W        48
#define ICON_H        48
#define BYTES_PER_PX  2
#define ICON_PIXELS   (ICON_W * ICON_H)
#define ICON_BYTES    (ICON_PIXELS * BYTES_PER_PX)

// file name constants
#define CHAT_REQUEST_FILENAME             "/chat_req.wav"
#define TRANSLATE_REQUEST_FILENAME        "/xlate_req.wav"
#define CHAT_RESPONSE_FILENAME            "/chat_response.wav"
#define TRANSLATE_RESPONSE_FILENAME       "/xlate_response.wav"

/**
 * @brief Icon wheel - icons appear in ascending order in a clockwise direction.
 */
typedef enum {
    ICON_CLOCK = 0,
    ICON_ALARM,  
    ICON_CHATBOT,                
    ICON_TRANSLATE,
    ICON_AUDIO,       
    ICON_INTERCOM,       
    ICON_SETTINGS,      
    ICON_COUNT
} icon_id_t;

/**
 * @brief GUI event enums
 */
enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_CHAT_REQ,
   GUI_EVENT_TRANSLATE,
   GUI_EVENT_STOP,
} gui_event_type ;

/**
 * @brief Update replay label types
 */
enum {
    REPLAY_UPDATE_NONE=0,
    REPLAY_UPDATE_PLAY,
    REPLAY_UPDATE_STOP,
};

enum {
   ROLLER_FROM=0,
   ROLLER_TO
};

#define RGB565_RED      0xF800
#define RGB565_GREEN    0x07E0
#define RGB565_BLUE     0x001F
#define RGB565_YELLOW   0xFFE0
#define RGB565_MAGENTA  0xF81F
#define RGB565_CYAN     0x07FF
#define RGB565_WHITE    0xFFFF

// LCD backlight control params
#define BKLT_CHANNEL             0        // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ                600      // pwm period frequency in HZ
#define BKLT_RESOLUTION          8        // timer resolution - 8 bits

#define TFT_GREY                 0x5AEB

// Function Prototypes
bool guiInit(void);
int16_t getGUIevents(void);
void setBacklight(uint8_t bri);
void pageBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void inactivity_timer_cb(lv_timer_t *t);
void gui_page1(lv_obj_t *parent);
void gui_page2(lv_obj_t *parent);
void gui_page3(lv_obj_t *parent);
void gui_page4(lv_obj_t *parent);
void gui_page5(lv_obj_t *parent);
void gui_page55(lv_obj_t *parent);
void gui_page6(lv_obj_t *parent);
void gui_page7(lv_obj_t *parent);
void gui_page8(lv_obj_t *parent);

void openMessageBox(const char *title, const char *body_text, const char *btn1_text, const char *btn2_text);

void butn_event_cb(lv_event_t * e);
void refreshDateTime(void);
void updateWifiLabel(lv_obj_t *lbl);
void updateBatteryLabel(lv_obj_t *batlbl);
void updateChatProgress(int8_t progress);
static void playwav_callback(uint8_t i);
static void mbox_event_cb(lv_event_t * e);
void roller_event_cb(lv_event_t * e);
void updateTranslateProgress(int8_t progress);
uint8_t getLangIndex(uint8_t roller_id);
void setXToText(char *labtext, uint8_t lang_sel_to);
void setXFromText(char *labtext, uint8_t lang_sel_from);

// Icon functions
void launcher_btn_event_cb(lv_event_t *e);
const char *icon_label_text(icon_id_t id);

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
static lv_style_t style_home_butn_default;
static lv_style_t style_wifi_label;
static lv_style_t style_battery_label;
static lv_style_t icon_style_pressed;

// lvgl objects
static lv_obj_t *tileview;
static lv_obj_t *tv_page1;
static lv_obj_t *tv_page2;
static lv_obj_t *tv_page3;
static lv_obj_t *tv_page4;
static lv_obj_t *tv_page5;
static lv_obj_t *tv_page55;
static lv_obj_t *tv_page6;
static lv_obj_t *tv_page7;
static lv_obj_t *tv_page8;

// page 2 - digital clock
static lv_obj_t *time_label;
static lv_obj_t *ampm_label;
static lv_obj_t *date_label;
static lv_obj_t *set_time_butn;
static lv_obj_t *temp_label;
static lv_obj_t *clock_home_butn;
static lv_obj_t *clock_wifi_label;
static lv_obj_t *clock_battery_label;
static lv_timer_t *clock_timer;
static lv_timer_t *xlate_timer;

// page 3 - alarms
static lv_obj_t *alarm_home_butn;

// page 4 - chatbot
static lv_obj_t *chat_home_butn;
static lv_obj_t *chat_wifi_label;
static lv_obj_t *chat_battery_label;
static lv_timer_t *chat_timer;
static lv_obj_t *chat_progress_arc;
// static lv_obj_t *chat_arc_label;
static lv_obj_t *chat_replay_butn;
static lv_obj_t *chat_replay_symbol_label;
static lv_obj_t *start_chat_butn;
static lv_obj_t *start_chat_butn_label;

// Message Box properties
static lv_obj_t *msgbox1;

// page 5 - translate 
static lv_obj_t *xlate_title_label;
static lv_obj_t *xlate_wifi_label;
static lv_obj_t *xlate_battery_label;
static lv_obj_t *xlate_home_butn;
static lv_obj_t *start_xlate_butn;
static lv_obj_t *xlate_progress_arc;
static lv_obj_t *xlate_arc_label;
static lv_obj_t *xfrom_roller;
static lv_obj_t *xto_roller;
static lv_obj_t *lang_swap_butn;
static lv_obj_t *start_xlate_butn_label;

// page 55
static lv_obj_t *xlate_out_title_label;


// extern's
extern language_info_t LanguageArray[];