/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <TFT_eSPI.h>                     // LCD hardware drive
#include <lvgl.h>
#include "sdcard.h"

// Touchpoint struct
typedef struct {
   int16_t x;
   int16_t y;
} touch_point_t ;

#define SCROLL_SIGN (+1)

typedef struct {
   lv_point_t touch_pos;                  // initial touch position for drag operation
   lv_point_t drag_obj_pos;               // initial object position 
   bool parent_scrollable;                // remember parent scroll mode
   void (*drop_cb)(void);                 // pointer to user function when object is dropped 
} drag_ctx_t;

// ---------- Node model ----------
struct TreeNode {
   std::string path;
   std::string name;
   bool is_dir = false;

   lv_obj_t* row_btn = nullptr;       // the clickable row
   lv_obj_t* arrow_lbl = nullptr;     // shows ▶ / ▼
   lv_obj_t* child_cont = nullptr;    // container holding children
   bool loaded = false;               // children added?
   int depth = 0;
};

#define FT6336_ADDR             0x38      

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

#define COLOR_BUFFER_SIZE  (SCREEN_WIDTH * 120)   // number of display lines in buffer
#define TFT_GREY 0x5AEB

enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_START_SPEED_TEST,
   GUI_EVENT_START_FORMAT,
   GUI_EVENT_CREATE_CCF,
};


// Function Prototypes
uint8_t readFT6336(touch_point_t* points, uint8_t maxPoints, uint8_t scrn_rotate);
bool guiInit(void);
int16_t getGUIevents(void);
void setBacklight(uint8_t bri);
void demoBuilder(void);
void setDefaultStyles(void);
void tileview_change_cb(lv_event_t *e);
void demo_page1(lv_obj_t *parent);
void demo_page2(lv_obj_t *parent);
void demo_page3(lv_obj_t *parent);
void demo_page4(lv_obj_t *parent);
void butn_event_cb(lv_event_t * e);
static void dd_event_handler(lv_event_t * e);
void updateDDList(char *list_items);
void fileListAddFile(String filename);
void fileListAddDir(String dirname);
void set_write_needle_line_value(void * obj, int32_t v);
void set_read_needle_line_value(void * obj, int32_t v);
void updateWriteSpeed(float mbs);
void updateReadSpeed(float mbs);
void OK_cancel_msgbox(const char *butn_text);
void msgbox_event_cb(lv_event_t * e);
void disableFormatButn(void);
void enabFormatButn(void);

// lvgl display driver ----------------------------------------------
#define ROWS_PER_BUF       26   // LVGL draw buffer height (PSRAM)
#define LINES_PER_CHUNK    20  // internal bounce chunk (SRAM)
#define LV_BUFFER_SIZE (SCREEN_WIDTH * ROWS_PER_BUF)   // 26 lines of 320 horiz pixels
static lv_display_t *main_disp;              // LVGL display object

// Internal DMA-capable (or just SRAM-capable) line buffer
static uint16_t *dma_linebuf;
// Uses PSRAM for draw buffers (saves a lot of SRAM), see 'guiInit()'
static lv_color_t *buf1;      // 1st display buffer (PSRAM)
static lv_color_t *buf2;      // 2nd display buffer (PSRAM)
// ------------------------------------------------------------------

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

static lv_obj_t *ddl;
static lv_obj_t *file_list;
static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;

static lv_obj_t *write_meter;
static lv_obj_t *read_meter;
static lv_obj_t *write_needle_line;
static lv_obj_t *read_needle_line;

static lv_obj_t *start_butn;
static lv_obj_t *write_speed_label;
static lv_obj_t *read_speed_label;

static lv_obj_t *format_butn;
static lv_obj_t *create_cf_butn;
static lv_obj_t *msgbox1;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif




