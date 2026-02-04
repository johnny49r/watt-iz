/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "gdriver.h"
#include "sd_lvgl_fs.h"

#define SCROLL_SIGN (+1)

// Message Box key codes
enum {
   MBOX_BTN_NONE=0,
   MBOX_BTN_OK,
   MBOX_BTN_CANCEL,
};

// Drag & Drop struct
typedef struct {
   lv_point_t touch_pos;                  // initial touch position for drag operation
   lv_point_t drag_obj_pos;               // initial object position 
   bool parent_scrollable;                // remember parent scroll mode
   void (*drop_cb)(void);                 // pointer to user function when object is dropped 
} drag_ctx_t;

// #define FT6336_ADDR             0x38      

// LCD backlight control params
#define BKLT_CHANNEL             0        // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ                600      // pwm period frequency in HZ
#define BKLT_RESOLUTION          8        // timer resolution - 8 bits

#define TFT_GREY                 0x5AEB

// Function Prototypes
// uint8_t readFT6336(touch_point_t* points, uint8_t maxPoints, uint8_t scrn_rotate);
bool guiInit(void);
void setBacklight(uint8_t bri);
void touch_calibrate(void);
void displayCornerCarot(uint8_t corner);
void pageBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void demo_page3(lv_obj_t *parent);
void demo_page4(lv_obj_t *parent);
void demo_page5(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
static inline void set_label_flag(lv_obj_t *obj, uint8_t value);
static inline uint8_t get_label_flag(lv_obj_t *obj);
void make_draggable_absolute(lv_obj_t *drag_obj);

void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag);

void closeMessageBox(void);
uint16_t getMessageBoxBtn(void);

void mbox_event_cb(lv_event_t * e);
static void drag_abs_cb(lv_event_t * e);
void drop_handler_cb(void);
static void switch_event_cb(lv_event_t * e);
static void slider_event_cb(lv_event_t * e);
static void roller_event_cb(lv_event_t * e);

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
static lv_obj_t *tv_demo_page5;
static lv_obj_t *touch_cal_butn;

static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;
static lv_obj_t *drag_drop_label;
static lv_obj_t *demo_sw;
static lv_obj_t *demo_switch_label;
static lv_obj_t *demo_slider;
static lv_obj_t *demo_roller;

static lv_obj_t *slider_label;

static lv_obj_t *msgbox1;

static lv_obj_t *page4_cont;
static lv_timer_t *calib_timer;
static lv_obj_t *line1;

// Draw corner carots for resistive touch calibration
static lv_point_precise_t UL_line_points[] = { {0, 0}, {25, 25}, {0, 0}, {15, 0}, {0, 0}, {0, 15} };
static lv_point_precise_t LL_line_points[] = { {0, 239}, {25, 239-25}, {0, 239}, {0, 239-15}, {0, 239}, {15, 239} };  
static lv_point_precise_t UR_line_points[] = { {319, 0}, {319-25, 25}, {319, 0}, {319-15, 0}, {319, 0}, {319, 15} };        
static lv_point_precise_t LR_line_points[] = { {319, 239}, {319-25, 239-25}, {319, 239}, {319, 239-15}, {319, 239}, {319-15, 239} };   
static lv_point_precise_t END_line_points[] = { {0, 0} };

typedef struct {
   uint32_t x_accum;
   uint32_t y_accum;
} calib_accum_t ;



