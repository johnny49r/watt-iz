/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// Global variables
bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
int16_t gui_event;
uint16_t msgBoxBtnTag = MBOX_BTN_NONE;


/********************************************************************
 * Display flush callback for LVGL graphics engine v9.3.x
 */
void display_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) 
{
   const int32_t x1 = area->x1;
   const int32_t y1 = area->y1;
   const int32_t w  = area->x2 - area->x1 + 1;
   const int32_t h  = area->y2 - area->y1 + 1;

   // px_map points to RGB565 pixels (because we set the display color format to RGB565)
   // const uint16_t *src = (const uint16_t*)px_map;
   const uint16_t *src = reinterpret_cast<const uint16_t *>(px_map);   

   // Copy in small DMA-friendly strips from PSRAM → SRAM, then push
   int y = 0;
   while (y < h) {
      const int lines = min(SRAM_LINES, h - y);
      const size_t pixels = (size_t)w * lines;

      // Copy PSRAM -> SRAM (DMA-capable)
      memcpy(sram_linebuf, src + (size_t)y * w, pixels * sizeof(uint16_t));

      // LovyanGFX: push strip (DMA inside)
      gfx.pushImage(x1, y1 + y, w, lines, sram_linebuf);

      y += lines;
   }
   lv_disp_flush_ready(disp_drv);
}


/********************************************************************
 * Callback to read touch screen 
 */
bool touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) 
{
   touch_point_t points[2];

   uint8_t touches = readTouchScreen((touch_point_t *)&points, SCREEN_ROTATION);
   if(touches > 0) {
      data->point.x = points[0].x;
      data->point.y = points[0].y;
      data->state = LV_INDEV_STATE_PRESSED;
   } else {
      data->state = LV_INDEV_STATE_RELEASED;
   }   
   return (touches > 0);                        // touched if true
}


/********************************************************************
 * Tick callback: runs every 1ms
 */
static void lv_tick_task(void *arg) {
   lv_tick_inc(1);
}


/********************************************************************
*  @brief Initialize graphics
* 
*  @return true on success
*/
bool guiInit(void)
{
   gui_event = GUI_EVENT_NONE;

   // Initialize LCD GPIO's
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off   
   vTaskDelay(100);
 
   // Configure TFT backlight dimming PWM
   ledcSetup(BKLT_CHANNEL, BKLT_FREQ, BKLT_RESOLUTION);
   ledcAttachPin(PIN_LCD_BKLT, BKLT_CHANNEL);  // attach the channel to GPIO pin to control dimming      
   setBacklight(0);                       // zero brightness

   // Initialize I2C peripheral
   if(!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)) { // init I2C for capacitive touch
      Serial.println(F("ERROR: I2C Init Failed!"));
      return false;
   }
   Wire.setClock(400000);                 // 400 khz I2C clk rate

   // Use this instead of Arduino 'Preferences'
   NVS_OK = NVS.begin();                  // init Non-Volatile storage like - 'EEPROM' 
   if(!NVS_OK) {
      Serial.println(F("ERROR: NVS Init Failed!"));
      return false;      
   }

   // Init display driver
   gfx.init();
   gfx.setRotation(3);
   gfx.setColorDepth(16);
   gfx.fillScreen(TFT_BLACK);
   gfx.setSwapBytes(false); // ILI9341 usually expects RGB565 as-is; enable if colors look swapped

   // Init LVGL graphics library
   lv_init();

   // Create a hardware timer to run lv_tick_task every 1ms
   const esp_timer_create_args_t lvgl_tick_args = {
      .callback = &lv_tick_task,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick"
   };
   esp_timer_handle_t lvgl_tick_timer;
   esp_timer_create(&lvgl_tick_args, &lvgl_tick_timer);
   esp_timer_start_periodic(lvgl_tick_timer, 1000); // 1000 µs = 1 ms

   // Create display buffers
   main_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);

   lv_display_set_color_format(main_disp, LV_COLOR_FORMAT_RGB565_SWAPPED); //LV_COLOR_FORMAT_RGB565);
   lv_display_set_flush_cb(main_disp, display_flush_cb);  

   // --- Allocate PSRAM draw buffers for LVGL partial mode ---
   // Total pixels per buffer = width * PSRAM_LINES
   uint32_t buf_bytes = (SCREEN_WIDTH * PSRAM_LINES) * sizeof(uint16_t);
   lv_buf1_psram = (uint16_t *)heap_caps_aligned_alloc(16, (uint32_t)buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   lv_buf2_psram = (uint16_t *)heap_caps_aligned_alloc(16, (uint32_t)buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

   // Supply buffers to LVGL (double-buffered, partial rendering)
   lv_display_set_buffers(main_disp,
         lv_buf1_psram, lv_buf2_psram,
         buf_bytes,
         LV_DISPLAY_RENDER_MODE_PARTIAL);

   // Allocate a small DMA-capable SRAM bounce buffer for speed.
   size_t sram_bytes = (SCREEN_WIDTH * SRAM_LINES) * sizeof(uint16_t);
   sram_linebuf = (uint16_t *)heap_caps_malloc(sram_bytes,
         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   

   // Get the active screen object for this display
   lv_obj_t *scr = lv_display_get_screen_active(main_disp);

   // Set background color and opacity
   lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN); // 
   lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 100);

   // Create a new pointer input device
   lv_indev_t * indev = lv_indev_create();
   lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
   lv_indev_set_read_cb(indev, (lv_indev_read_cb_t)touch_read_cb);

   return true;   
}


/********************************************************************
*  @brief Set LCD backlight brightness
* 
*  @param bri - 0 - 255
*/
void setBacklight(uint8_t bri)
{
   ledcWrite(BKLT_CHANNEL, bri);                   // set PWM value  
}


/********************************************************************
*  @brief Pre-build all tiled pages. Tileview pages can be scrolled
*  manually or by code.   
*/
void demoBuilder(void)
{
   tileview = lv_tileview_create(lv_screen_active());
   lv_obj_add_style(tileview, &style_tileview, LV_PART_MAIN);
   lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
   lv_obj_add_event_cb(tileview, tileview_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

   // Create tiles
   tv_demo_page1 =  lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
   tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);   
   tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);    
   tv_demo_page4 =  lv_tileview_add_tile(tileview, 3, 0, LV_DIR_LEFT);      

   // build tile pages
   demo_page1(tv_demo_page1);             // build the top level switch page
   demo_page2(tv_demo_page2); 
   demo_page3(tv_demo_page3);    
   demo_page4(tv_demo_page4);    

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_ON);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   LV_IMAGE_DECLARE(File_System_Logo_300x150); 

   // show demo info
   lv_obj_t *scrn1_demo_label = lv_label_create(parent);
   lv_obj_set_size(scrn1_demo_label, 320, 35);
   lv_obj_add_style(scrn1_demo_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1_demo_label, 0, LV_PART_MAIN);
   lv_label_set_text(scrn1_demo_label, "WATT-IZ DEMO PROJECTS");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 4);      

   // Project demo image
   lv_obj_t * img1 = lv_image_create(parent);
   lv_image_set_src(img1, &File_System_Logo_300x150);
   lv_obj_align_to(img1, scrn1_demo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4); 

   // Swipe label button
   lv_obj_t *swipe_label = lv_label_create(parent);
   lv_obj_set_size(swipe_label, 120, 36);
   lv_obj_center(swipe_label);
   lv_label_set_text(swipe_label, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   lv_obj_set_style_text_color(swipe_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_align(swipe_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(swipe_label, &lv_font_montserrat_20, LV_PART_MAIN);   
   lv_obj_align(swipe_label, LV_ALIGN_BOTTOM_MID, 0, 0);  
}


/********************************************************************
 * Demo Page 2 - directory display
 */
void demo_page2(lv_obj_t *parent)
{
   file_expl = lv_file_explorer_create(parent);
   lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);

   // get parts
   fe_tbl = lv_file_explorer_get_file_table(file_expl);
   fe_hdr = lv_file_explorer_get_header(file_expl);
   fe_path_lbl = lv_file_explorer_get_path_label(file_expl);

   // Header / top bar
   lv_obj_set_style_bg_color(fe_hdr, lv_palette_darken(LV_PALETTE_BLUE, 2), LV_PART_MAIN);
   lv_obj_set_style_text_font(fe_hdr, &lv_font_montserrat_18, LV_PART_MAIN);
   // Make header use flexbox row layout
   lv_obj_set_flex_flow(fe_hdr, LV_FLEX_FLOW_ROW);
   lv_obj_set_style_pad_all(fe_hdr, 4, LV_PART_MAIN);

   // Ensure elements spread out across the row
   //  -> home button left, path label center, up button right
   lv_obj_set_flex_align(
      fe_hdr,
      LV_FLEX_ALIGN_CENTER,           // cross axis vertical alignment
      LV_FLEX_ALIGN_SPACE_BETWEEN,   // main axis: spread evenly to edges
      LV_FLEX_ALIGN_CENTER           // cross axis vertical alignment
   );

   // Path label (on header)
   lv_obj_set_flex_grow(fe_path_lbl, 1);  // let it expand to fill middle space
   lv_label_set_long_mode(fe_path_lbl, LV_LABEL_LONG_CLIP);
   lv_obj_set_style_text_color(fe_path_lbl, lv_color_hex(0xFFFFFF), 0);
   lv_obj_set_style_text_align(fe_path_lbl, LV_TEXT_ALIGN_LEFT, 0);
   lv_obj_set_style_pad_top(fe_path_lbl, 6, LV_PART_MAIN);

   btn_home = lv_button_create(fe_hdr);
   lv_obj_t *lbl_home = lv_label_create(btn_home);
   lv_label_set_text(lbl_home, LV_SYMBOL_HOME);
   lv_obj_add_event_cb(btn_home, go_home, LV_EVENT_CLICKED, NULL);
   lv_obj_set_style_bg_color(btn_home, lv_palette_darken(LV_PALETTE_GREY, 4), LV_PART_MAIN);

   // Apply dark theme
   lv_obj_set_style_bg_color(file_expl, lv_color_hex(0x1E1E1E), 0);
   lv_obj_set_style_text_color(file_expl, lv_color_hex(0xE0E0E0), 0);

   // File list (table)
   lv_obj_set_style_bg_color(fe_tbl, lv_color_hex(0x181818), LV_PART_ITEMS);
   lv_obj_set_style_text_color(fe_tbl, lv_color_hex(0xE0E0E0), LV_PART_ITEMS);
   lv_obj_set_style_border_width(fe_tbl, 0, 0);

   // Row visuals (selected/focused/hovered)
   lv_obj_set_style_bg_color(fe_tbl, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_ITEMS | LV_STATE_FOCUSED);
   lv_obj_set_style_bg_color(fe_tbl, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_ITEMS | LV_STATE_CHECKED);

   // Optional: alternating rows (use your own scheme)
   lv_obj_set_style_bg_opa(fe_tbl, LV_OPA_COVER, LV_PART_ITEMS);

   // register event callbacks
   lv_obj_add_event_cb(fe_tbl, table_event_cb, LV_EVENT_ALL, NULL);
   lv_obj_add_event_cb(file_expl, explorer_event_cb, LV_EVENT_ALL, NULL);

   lv_file_explorer_set_sort(file_expl, LV_EXPLORER_SORT_KIND);
   lv_file_explorer_open_dir(file_expl, "S:/");

   // lv_file_explorer_set_quick_access_path(file_explorer, LV_EXPLORER_FS_DIR, "S:/");
}


/********************************************************************
 * @brief Helper functions for file explorer navigation
 */ 
#define MAX_PATH_LEN       128
void go_home(lv_event_t *e) 
{
    LV_UNUSED(e);
    set_path("S:/");                      // set path to root
}

void set_path(const char *path) 
{
   lv_strncpy(current_path, path, sizeof(current_path));
   lv_file_explorer_open_dir(file_expl, current_path);     // repopulates the table
   // update header label
   lv_label_set_text(fe_path_lbl, current_path);
}

bool is_dir_name(const char *name) 
{
    if(!name) return false;
    size_t n = strlen(name);
    return n > 0 && name[n-1] == '/';    // lv_file_explorer lists dirs with trailing '/'
}

// join current_path + entry name (entry may already end with '/')
void join_path(char *out, size_t outsz, const char *base, const char *name) 
{
    // ensure base ends with '/'
    bool base_has_slash = base[strlen(base)-1] == '/';
    if(base_has_slash) 
      lv_snprintf(out, outsz, "%s%s", base, name);
    else               
      lv_snprintf(out, outsz, "%s/%s", base, name);
}

// open folders when user selects a row in the file table
void table_event_cb(lv_event_t *e) 
{
   lv_event_code_t code = lv_event_get_code(e);
   if(code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_SHORT_CLICKED) {
      return;
   }

   uint32_t row, col;
   lv_table_get_selected_cell(fe_tbl, &row, &col);
   if(col != 0) return;   // names are in column 0

   const char *name = lv_table_get_cell_value(fe_tbl, row, 0);
   if(!name || !is_dir_name((char *)name+5))    // skip past LV_SYMBOL_*
      return;

   // char * pname = (char*) heap_caps_malloc(256, MALLOC_CAP_SPIRAM); 
   // strncpy(pname, name, strlen(name));
   char next[MAX_PATH_LEN];
   join_path(next, sizeof(next), current_path, name);
      Serial.printf("next=%s\n", (char *)&next);
   set_path(next);
}

// optional: after directory loads, tweak visuals (icons etc.)
void explorer_event_cb(lv_event_t *e) 
{
    if(lv_event_get_code(e) != LV_EVENT_READY) return;

    // example: ensure table selection mode is single & highlight enabled
   //  lv_table_set_sel_mode(tbl, LV_TABLE_SEL_MODE_SINGLE);
    // you can also rewrite icons here if you want (folders/files)
}


/********************************************************************
 * Demo Page 3 - Execute a simple SD Card speed test. 
 */
void demo_page3(lv_obj_t *parent)
{
   static const char *labels[] = {"0","1","2","3","4","5","6","7","8","9","10", NULL};

   // Header label 
   lv_obj_t *hdr_label = lv_label_create(parent);
   lv_obj_set_size(hdr_label, 315, 36);
   lv_label_set_text(hdr_label, "SD Card Speed Test");
   lv_obj_set_style_text_color(hdr_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_align(hdr_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(hdr_label, &lv_font_montserrat_22, LV_PART_MAIN);   
   lv_obj_align(hdr_label, LV_ALIGN_TOP_MID, 0, 0);  

   // label for write speed
   write_speed_label = lv_label_create(parent);
   lv_obj_set_size(write_speed_label, 115, 32);
   lv_obj_add_style(write_speed_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(write_speed_label, 1, LV_PART_MAIN);
   lv_obj_align(write_speed_label, LV_ALIGN_BOTTOM_LEFT, 0, -10);
   lv_label_set_text(write_speed_label, "00.00 Mbs");

   /**
    * @brief Create Write Arc for displaying relative write speed
    */
   write_arc = lv_arc_create(parent);
   lv_obj_set_size(write_arc, 120, 120);
   lv_arc_set_rotation(write_arc, 135);
   lv_arc_set_bg_angles(write_arc, 0, 270);
   lv_arc_set_value(write_arc, 0);
   lv_obj_add_event_cb(write_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
   lv_obj_align(write_arc, LV_ALIGN_LEFT_MID, 20, -10);
   lv_obj_clear_flag(write_arc, LV_OBJ_FLAG_CLICKABLE);
   lv_obj_clear_flag(write_arc, LV_OBJ_FLAG_SCROLLABLE);

   // Manually update the label for the first time
   lv_obj_send_event(write_arc, LV_EVENT_VALUE_CHANGED, NULL);

   // write meter label - static label under meter
   lv_obj_t *wrarc_label = lv_label_create(write_arc);
   lv_obj_set_size(wrarc_label, 70, 30);
   lv_obj_add_style(wrarc_label, &style_label_default, LV_PART_MAIN | LV_STATE_DEFAULT);   
   lv_label_set_text(wrarc_label, "WRITE");
   lv_obj_set_style_text_align(wrarc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_align(wrarc_label, LV_ALIGN_CENTER, 0, 0);   

   // label for read speed
   read_speed_label = lv_label_create(parent);
   lv_obj_set_size(read_speed_label, 115, 32);
   lv_obj_add_style(read_speed_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(read_speed_label, 1, LV_PART_MAIN);   
   lv_obj_align(read_speed_label, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
   lv_label_set_text(read_speed_label, "00.00 Mbs");

   /**
    * @brief Create Read Arc for displaying relative write speed
    */
   read_arc = lv_arc_create(parent);
   lv_obj_set_size(read_arc, 120, 120);
   lv_arc_set_rotation(read_arc, 135);
   lv_arc_set_bg_angles(read_arc, 0, 270);
   lv_arc_set_value(read_arc, 0);
   lv_obj_add_event_cb(read_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
   lv_obj_align(read_arc, LV_ALIGN_RIGHT_MID, -20, -10);
   lv_obj_clear_flag(read_arc, LV_OBJ_FLAG_CLICKABLE);   
   lv_obj_clear_flag(read_arc, LV_OBJ_FLAG_SCROLLABLE);

   // Manually update the label for the first time
   lv_obj_send_event(read_arc, LV_EVENT_VALUE_CHANGED, NULL);   

   // write meter label - static label under meter
   lv_obj_t *rdarc_label = lv_label_create(read_arc);
   lv_obj_set_size(rdarc_label, 70, 30);
   lv_obj_add_style(rdarc_label, &style_label_default, LV_PART_MAIN | LV_STATE_DEFAULT);   
   lv_label_set_text(rdarc_label, "READ");
   lv_obj_set_style_text_align(rdarc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_align(rdarc_label, LV_ALIGN_CENTER, 0, 0);     

   // Start test button
   start_butn = lv_btn_create(parent);
   lv_obj_set_size(start_butn, 70, 36);
   lv_obj_add_style(start_butn, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(start_butn, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);  
   lv_obj_set_style_border_width(start_butn, 1, LV_PART_MAIN);    
   lv_obj_set_style_border_color(start_butn, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);    
   lv_obj_set_style_radius(start_butn, 6, LV_PART_MAIN);   // radius corners
   lv_obj_set_style_pad_top(start_butn, 4, LV_PART_MAIN);
   lv_obj_align(start_butn, LV_ALIGN_BOTTOM_MID, 0, -10);   
   lv_obj_add_event_cb(start_butn, &butn_event_cb, LV_EVENT_CLICKED, NULL);    

   // Start button label
   lv_obj_t *start_label = lv_label_create(start_butn);
   lv_obj_set_size(start_label, 72, 32);
   lv_label_set_text(start_label, "START"); 
   lv_obj_set_style_text_font(start_label, &lv_font_montserrat_18, LV_PART_MAIN); 
   lv_obj_set_style_text_align(start_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_set_style_pad_top(start_label, 5, LV_PART_MAIN);
   lv_obj_center(start_label);
}


/********************************************************************
 * Demo Page 4 - Format SD Card
 */
void demo_page4(lv_obj_t *parent)
{
   // Header label 
   lv_obj_t *hdr_label = lv_label_create(parent);
   lv_obj_set_size(hdr_label, 315, 32);
   lv_label_set_text(hdr_label, "SD Card Format Utilities");
   lv_obj_set_style_text_color(hdr_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_align(hdr_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(hdr_label, &lv_font_montserrat_22, LV_PART_MAIN);   
   lv_obj_align(hdr_label, LV_ALIGN_TOP_MID, 0, 0);  

   // Format button
   format_butn = lv_btn_create(parent);
   lv_obj_set_size(format_butn, 110, 32);
   lv_obj_add_style(format_butn, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(format_butn, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);  
   lv_obj_set_style_border_width(format_butn, 1, LV_PART_MAIN);    
   lv_obj_set_style_border_color(format_butn, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);    
   lv_obj_set_style_radius(format_butn, 6, LV_PART_MAIN);   // radius corners
   lv_obj_set_style_pad_top(format_butn, 4, LV_PART_MAIN);
   lv_obj_align_to(format_butn, hdr_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);   
   lv_obj_add_event_cb(format_butn, &butn_event_cb, LV_EVENT_CLICKED, NULL);    

   // Format button label
   lv_obj_t *format_label = lv_label_create(format_butn);
   lv_obj_set_size(format_label, 110, 32);
   lv_label_set_text(format_label, "FORMAT"); 
   lv_obj_set_style_text_font(format_label, &lv_font_montserrat_18, LV_PART_MAIN); 
   lv_obj_set_style_text_align(format_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_set_style_pad_top(format_label, 5, LV_PART_MAIN);
   lv_obj_center(format_label);

   // Format button description label
   lv_obj_t *format_desc_label = lv_label_create(parent);
   lv_obj_set_size(format_desc_label, 310, 32);
   lv_obj_add_style(format_desc_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(format_desc_label, "Format SD Card to FaT32"); 
   // lv_obj_set_style_text_font(format_desc_label, &lv_font_montserrat_18, LV_PART_MAIN); 
   lv_obj_set_style_text_align(format_desc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   // lv_obj_set_style_pad_top(format_desc_label, 5, LV_PART_MAIN);
   // lv_obj_set_style_radius(format_desc_label, 6, LV_PART_MAIN);   
   lv_obj_set_style_border_width(format_desc_label, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(format_desc_label, lv_color_white(), LV_PART_MAIN);     
   lv_obj_align_to(format_desc_label, format_butn, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);  

   // Create config file button
   create_cf_butn = lv_btn_create(parent);
   lv_obj_set_size(create_cf_butn, 110, 32);
   lv_obj_add_style(create_cf_butn, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(create_cf_butn, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);  
   lv_obj_set_style_border_width(create_cf_butn, 1, LV_PART_MAIN);    
   lv_obj_set_style_border_color(create_cf_butn, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);    
   lv_obj_set_style_radius(create_cf_butn, 6, LV_PART_MAIN);   // radius corners
   lv_obj_set_style_pad_top(create_cf_butn, 4, LV_PART_MAIN);
   lv_obj_align_to(create_cf_butn, format_desc_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);   
   lv_obj_add_event_cb(create_cf_butn, &butn_event_cb, LV_EVENT_CLICKED, NULL);    

   // CCF button label
   lv_obj_t *ccf_label = lv_label_create(create_cf_butn);
   lv_obj_set_size(ccf_label, 110, 32);
   lv_label_set_text(ccf_label, "CREATE"); 
   lv_obj_set_style_text_font(ccf_label, &lv_font_montserrat_18, LV_PART_MAIN); 
   lv_obj_set_style_text_align(ccf_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_set_style_pad_top(ccf_label, 5, LV_PART_MAIN);   
   lv_obj_center(ccf_label);

   // CCF button description label
   lv_obj_t *ccf_desc_label = lv_label_create(parent);
   lv_obj_set_size(ccf_desc_label, 310, 32);
   lv_obj_add_style(ccf_desc_label, &style_label_default, LV_PART_MAIN);   
   lv_label_set_text(ccf_desc_label, "Create New Config File"); 
   // lv_obj_set_style_text_font(ccf_desc_label, &lv_font_montserrat_18, LV_PART_MAIN); 
   lv_obj_set_style_text_align(ccf_desc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   // lv_obj_set_style_pad_top(format_desc_label, 5, LV_PART_MAIN);
   // lv_obj_set_style_radius(ccf_desc_label, 6, LV_PART_MAIN);     
   lv_obj_set_style_border_width(ccf_desc_label, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(ccf_desc_label, lv_color_white(), LV_PART_MAIN);   
   lv_obj_align_to(ccf_desc_label, create_cf_butn, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);    
}


/********************************************************************
 * @brief Handle events from an arc widget
 */
static void arc_event_cb(lv_event_t * e)
{
   lv_obj_t * arc = lv_event_get_target_obj(e);
   lv_obj_t * label = (lv_obj_t *)lv_event_get_user_data(e);

}


/********************************************************************
 * @brief Set needle on WRITE meter.
 */
void set_write_needle_line_value(void * obj, int32_t v)
{
   //  lv_scale_set_line_needle_value((lv_obj_t *)obj, write_needle_line, 56, v);
}


/********************************************************************
 * @brief Set needle on READ meter.
 */
void set_read_needle_line_value(void * obj, int32_t v)
{
   //  lv_scale_set_line_needle_value((lv_obj_t *)obj, read_needle_line, 56, v);
}


/********************************************************************
 * @brief Calculate WRITE speed values
 */
void updateWriteSpeed(float mbs)
{
   char buf[16];
   int32_t needle_val;

   sprintf(buf, "%2.2f MBs", mbs);
   lv_label_set_text(write_speed_label, buf);
   lv_arc_set_value(write_arc, int(mbs * 10));
   // lv_obj_invalidate(write_meter);      // the lv_scale (dial)
}


/********************************************************************
 * @brief Calculate READ speed values
 */
void updateReadSpeed(float mbs)
{
   char buf[16];
   int32_t needle_val;

   sprintf(buf, "%2.2f MBs", mbs);
   lv_label_set_text(read_speed_label, buf);
   lv_arc_set_value(read_arc, int(mbs * 10));
   // lv_obj_invalidate(read_meter);      // the lv_scale (dial)   
}


/********************************************************************
 * @brief Update the file dropdown list
 */
void updateDDList(char *list_items)
{
   // lv_dropdown_set_options(ddl, list_items);
}


/********************************************************************
 * @brief Add new File to the file list
 */
void fileListAddFile(char * filestr) 
{
   // lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_FILE, filestr);
   // lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN);   
   // lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);  
}


/********************************************************************
 * @brief Add new Directory to the file list
 */
void fileListAddDir(char *dirstr)
{
   // lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_RIGHT, dirstr); 
   // lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
   // lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_BLUE, 3), LV_PART_MAIN);   
}


/********************************************************************
 * DropDown widget event handler
 */
// static void dd_event_handler(lv_event_t * e)
// {
//     lv_event_code_t code = lv_event_get_code(e);
//     lv_obj_t * obj = lv_event_get_target_obj(e);
//     if(code == LV_EVENT_VALUE_CHANGED) {
//         char buf[32];
//         lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
//         LV_LOG_USER("Option: %s", buf);
//         lv_dropdown_set_selected(obj, 0);
//     }
// }


/********************************************************************
*  @fn Butn event handler. Comes here on button click or ? event.
*/
void butn_event_cb(lv_event_t * e)
{
   uint16_t i;

   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(code == LV_EVENT_CLICKED) {
      if(butn == start_butn) {
         gui_event = GUI_EVENT_START_SPEED_TEST;
      }
      else if(butn == format_butn) {
         OK_cancel_msgbox("FORMAT");
      }
      else if(butn == create_cf_butn) {
         OK_cancel_msgbox("CREATE");
      }
   }
}


/********************************************************************
 * @brief Launch a modal message box 
 */
void msgbox_event_cb(lv_event_t * e)
{
   char buf[16];
   lv_obj_t * btn = lv_event_get_target_obj(e);
   lv_obj_t * label = lv_obj_get_child(btn, 0);
   strncpy(buf, lv_label_get_text(label), sizeof(buf));
      Serial.printf("msgbox1 btn '%s' clicked\n", buf);
   // Serial.printf("Button %s clicked", lv_label_get_text(label));
   if(strcmp(buf, "FORMAT") == 0) {
      gui_event = GUI_EVENT_START_FORMAT;  
   } else if(strcmp(buf, "CREATE") == 0) {
      gui_event = GUI_EVENT_CREATE_CCF; 
   }
   lv_msgbox_close(msgbox1);     // delete the message box      
}


/********************************************************************
 * @brief Disable format_butn. Used to show formatting in progress.
 */
void disableFormatButn(void)
{
   lv_obj_add_state(format_butn, LV_STATE_DISABLED);
}


/********************************************************************
 * @brief Enable format_butn. 
 */
void enabFormatButn(void)
{
   lv_obj_clear_state(format_butn, LV_STATE_DISABLED);
}


/********************************************************************
 * @brief Create a message box to verify if the user really wants to 
 * format the SD Card.
 */
void OK_cancel_msgbox(const char *butn_text)
{
   msgbox1 = lv_msgbox_create(NULL);

   if(strcmp(butn_text, "FORMAT") == 0) {
      lv_msgbox_add_text(msgbox1, "***WARNING: Formatting will delete all contents of the SD Card. Press FORMAT to continue");
      lv_msgbox_add_title(msgbox1, "Format SD Card");
   }
   else if(strcmp(butn_text, "CREATE") == 0) {
      lv_msgbox_add_text(msgbox1, "***WARNING: Create new Configuration File will overwrite the contents of the old file. Are you sure?");
      lv_msgbox_add_title(msgbox1, "Create Config File");
   }
   lv_msgbox_add_close_button(msgbox1);

   lv_obj_t * btn;
   btn = lv_msgbox_add_footer_button(msgbox1, butn_text);
   lv_obj_add_event_cb(btn, msgbox_event_cb, LV_EVENT_CLICKED, NULL);
   btn = lv_msgbox_add_footer_button(msgbox1, "Cancel");
   lv_obj_add_event_cb(btn, msgbox_event_cb, LV_EVENT_CLICKED, NULL);
   return;
}

/********************************************************************
 * Check if GUI is reporting any events (button clicks, etc.)
 */
int16_t getGUIevents(void)
{
   int16_t gev = gui_event;

   gui_event = GUI_EVENT_NONE;
   return gev;
}


/********************************************************************
*  @brief Tileview page change event handler
* 
*  @param e - event obj
*/
void tileview_change_cb(lv_event_t *e)
{
   lv_obj_t *_tileview = (lv_obj_t *)lv_event_get_target(e);
   lv_obj_t *cur_tile = lv_tileview_get_tile_act(_tileview);    // active tile (page)
   lv_event_code_t code = lv_event_get_code(e);
}


/********************************************************************
*  @brief Call here to initialize default Styles for various widgets
*/
void setDefaultStyles()
{
   /**
    *  @brief Init style for tileview object
    */
   lv_style_init(&style_tileview);
   lv_style_set_bg_color(&style_tileview, lv_color_black());

   /**
   *  @brief Init the start button default style
   */
   lv_style_init(&style_butn_released);
   lv_style_set_bg_opa(&style_butn_released, LV_OPA_100);
   lv_style_set_bg_color(&style_butn_released, lv_palette_main(LV_PALETTE_BLUE));
   lv_style_set_border_width(&style_butn_released, 1);
   lv_style_set_border_color(&style_butn_released, lv_palette_darken(LV_PALETTE_ORANGE, 1));
   lv_style_set_shadow_width(&style_butn_released, 0);
   lv_style_set_outline_width(&style_butn_released, 0);       // no outline 
   lv_style_set_pad_all(&style_butn_released, 1);

   /**
   *  @brief Init the start button pressed style
   */
   lv_style_init(&style_butn_pressed);
   lv_style_set_bg_color(&style_butn_pressed, lv_palette_darken(LV_PALETTE_BLUE, 1));
   lv_style_set_border_width(&style_butn_pressed, 0);
   lv_style_set_pad_all(&style_butn_pressed, 0);   
   lv_style_set_shadow_width(&style_butn_pressed, 0); 

   lv_style_set_outline_pad(&style_butn_pressed, 1);
   lv_style_set_outline_width(&style_butn_pressed, 5);
   lv_style_set_outline_color(&style_butn_pressed, lv_palette_lighten(LV_PALETTE_PINK, 2)); 
   lv_style_set_outline_opa(&style_butn_pressed, LV_OPA_90);

   /**
   *  @brief Init the default style of a label
   */
   lv_style_init(&style_label_default);
   lv_style_set_radius(&style_label_default, 6);
   lv_style_set_bg_color(&style_label_default, lv_color_black()); 
   lv_style_set_bg_opa(&style_label_default, LV_OPA_0);
   lv_style_set_pad_top(&style_label_default, 3);
   lv_style_set_pad_left(&style_label_default, 4);  

   lv_style_set_border_width(&style_label_default, 0);         // 1 pix border
   lv_style_set_border_color(&style_label_default, lv_color_white()); 
   lv_style_set_outline_width(&style_label_default, 0);        // no outline 

   lv_style_set_text_font(&style_label_default, &lv_font_montserrat_18);
   lv_style_set_text_color(&style_label_default, lv_color_white());
   lv_style_set_text_align(&style_label_default, LV_TEXT_ALIGN_LEFT);    
}


/********************************************************************
 * @brief Message Box popup
 */
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, 
      uint32_t btn1_tag, const char *btn2_text, uint32_t btn2_tag, 
      const char *btn3_text, uint32_t btn3_tag)
{
   msgbox1 = lv_msgbox_create(NULL);      // create a MODAL message box

   // customize title area   
   lv_msgbox_add_title(msgbox1, title_text);    // create title label first 
   lv_obj_t *title_lbl = lv_msgbox_get_title(msgbox1);
   lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
   lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x000000), LV_PART_MAIN);
   lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(title_lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);

   // Change the content area background and text color 
   lv_obj_set_style_bg_color(msgbox1, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(msgbox1, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_text_color(msgbox1, lv_color_white(), LV_PART_MAIN);   

   lv_msgbox_add_text(msgbox1, body_text);

   lv_obj_t * btn;
   if(strlen(btn1_text) > 0) {
      btn = lv_msgbox_add_footer_button(msgbox1, btn1_text);
      lv_obj_set_user_data(btn, (void *)(uintptr_t)btn1_tag);
      lv_obj_add_event_cb(btn, mbox_event_cb, LV_EVENT_CLICKED, NULL);   
   }
   if(strlen(btn2_text) > 0) {
      btn = lv_msgbox_add_footer_button(msgbox1, btn2_text);
      lv_obj_set_user_data(btn, (void *)(uintptr_t)btn2_tag);      
      lv_obj_add_event_cb(btn, mbox_event_cb, LV_EVENT_CLICKED, NULL);   
   }   
   if(strlen(btn3_text) > 0) {
      btn = lv_msgbox_add_footer_button(msgbox1, btn3_text);
      lv_obj_set_user_data(btn, (void *)(uintptr_t)btn3_tag);      
      lv_obj_add_event_cb(btn, mbox_event_cb, LV_EVENT_CLICKED, NULL);   
   }      
}


/********************************************************************
 * @brief Close an opened messagebox
 */
void closeMessageBox(void)
{
   if(msgbox1) {                          // is the msgbox open?
      lv_msgbox_close(msgbox1);           // close (destroy) it
      msgbox1 = nullptr;                  // mark it as unassigned
   }
}


/********************************************************************
 * @brief Message Box button event handler
 */
void mbox_event_cb(lv_event_t * e)
{
   lv_obj_t * btn = lv_event_get_target_obj(e);
   lv_obj_t * label = lv_obj_get_child(btn, 0);
   uint16_t op = (uint16_t)(uintptr_t)lv_obj_get_user_data(btn);
   char *btn_text = lv_label_get_text(label);
         // Serial.printf("Button %s clicked, user data=%d\n", btn_text, op);
   msgBoxBtnTag = op;                     // save a copy of the butn tag 

   switch(op) {
      case MBOX_BTN_OK:
      case MBOX_BTN_CANCEL:
         closeMessageBox();
         break;

   }
}


/********************************************************************
 * @brief Return message box button response.
 * @note Returns MBOX_BTN_NONE if no activity.
 */
uint16_t getMessageBoxBtn(void)
{
   uint16_t ltag = msgBoxBtnTag;
   msgBoxBtnTag = MBOX_BTN_NONE;
   return ltag;
}