/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <TFT_eSPI.h>                     // LCD hardware drive
#include <lvgl.h>

// Touchpoint struct
typedef struct {
   int16_t x;
   int16_t y;
} touch_point_t ;

#define SCROLL_SIGN (+1)

// Drag & Drop struct
typedef struct {
   lv_point_t touch_pos;                  // initial touch position for drag operation
   lv_point_t drag_obj_pos;               // initial object position 
   bool parent_scrollable;                // remember parent scroll mode
   void (*drop_cb)(void);                 // pointer to user function when object is dropped 
} drag_ctx_t;

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
void touch_calibrate(void);
void pageBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void demo_page3(lv_obj_t *parent);
void demo_page4(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
static inline void set_label_flag(lv_obj_t *obj, uint8_t value);
static inline uint8_t get_label_flag(lv_obj_t *obj);
void make_draggable_absolute(lv_obj_t *drag_obj);
static void drag_abs_cb(lv_event_t * e);
void drop_handler_cb(void);
static void switch_event_cb(lv_event_t * e);
static void slider_event_cb(lv_event_t * e);
static void roller_event_cb(lv_event_t * e);

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
static lv_obj_t *tv_demo_page4;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif

static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;
static lv_obj_t *drag_drop_label;
static lv_obj_t *demo_sw;
static lv_obj_t *demo_switch_label;
static lv_obj_t *demo_slider;
static lv_obj_t *demo_roller;

static lv_obj_t *slider_label;