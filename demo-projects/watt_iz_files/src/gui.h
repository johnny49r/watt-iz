/**
 * @brief gui.h
 */
#pragma once

#include "config.h"
#include <lvgl.h>
#include "gdriver.h"

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

// LCD backlight control params
#define BKLT_CHANNEL            0         // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ               600       // pwm period frequency in HZ
#define BKLT_RESOLUTION         8         // timer resolution - 8 bits

#define TFT_GREY 0x5AEB

enum {
   GUI_EVENT_NONE=0,
   GUI_EVENT_START_SPEED_TEST,
   GUI_EVENT_START_FORMAT,
   GUI_EVENT_CREATE_CCF,
};

// Message Box key codes
enum {
   MBOX_BTN_NONE=0,
   MBOX_BTN_OK,
   MBOX_BTN_CANCEL,
};

// Function Prototypes
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
void fileListAddFile(char *filestr);
void fileListAddDir(char *dirstr);
void set_write_needle_line_value(void * obj, int32_t v);
void set_read_needle_line_value(void * obj, int32_t v);
void updateWriteSpeed(float mbs);
void updateReadSpeed(float mbs);
void OK_cancel_msgbox(const char *butn_text);
void msgbox_event_cb(lv_event_t * e);
void disableFormatButn(void);
void enabFormatButn(void);
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag);
void closeMessageBox(void);  
uint16_t getMessageBoxBtn(void);    

static void arc_event_cb(lv_event_t * e);
void mbox_event_cb(lv_event_t * e);

// file explorer events
void go_home(lv_event_t *e);
// void go_up(lv_event_t *e);
void set_path(const char *path);
bool is_dir_name(const char *name);
void join_path(char *out, size_t outsz, const char *base, const char *name);
void table_event_cb(lv_event_t *e);
void explorer_event_cb(lv_event_t *e);

// lvgl display driver ----------------------------------------------
#define PSRAM_LINES                 SCREEN_HEIGHT //120   // PSRAM draw screen buffer (partial)
#define SRAM_LINES                  12     // SRAM line buffer (for speed)

// PSRAM LVGL draw buffers (two for double-buffered partial rendering)
static lv_display_t *main_disp;              // LVGL display object
static uint16_t *lv_buf1_psram = nullptr;
static uint16_t *lv_buf2_psram = nullptr;
// DMA-capable SRAM bounce buffer (used inside flush)
static uint16_t *sram_linebuf = nullptr;
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

// file explorer and items
static lv_obj_t *file_expl;
static lv_obj_t *fe_tbl;
static lv_obj_t *fe_hdr;
static lv_obj_t *fe_path_lbl;
static lv_obj_t *btn_up;
static lv_obj_t *btn_home;
static lv_obj_t *path_lbl;
static char current_path[128] = "S:/";   // start here; change to your root

static lv_obj_t *demo_butn1;
static lv_obj_t *demo_label1;
static lv_obj_t *demo_butn2;

static lv_obj_t *write_arc;
static lv_obj_t *read_arc;

static lv_obj_t *start_butn;
static lv_obj_t *write_speed_label;
static lv_obj_t *read_speed_label;

static lv_obj_t *format_butn;
static lv_obj_t *create_cf_butn;
static lv_obj_t *msgbox1;

#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   static lv_obj_t *touch_cal_butn;
#endif




