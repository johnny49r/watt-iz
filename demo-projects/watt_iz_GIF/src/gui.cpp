/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// Global Variables
bool NVS_OK = false;                      // global OK flag for non-volatile storage lib
int8_t res_calib_state = -1;              // for resistive touch calibration
int8_t next_calib_state = -1;
bool cal_screen_touched = false;
volatile uint16_t msgBoxBtnTag = MBOX_BTN_NONE;


/********************************************************************
 * @brief LVGL log output to Serial.printf...
 */
void my_lvgl_log_cb(lv_log_level_t level, const char * buf)
{
    Serial.print("[LVGL] ");
    Serial.println(buf);
}

/********************************************************************
 * Display flush callback for LVGL graphics engine v9.3.x
 */
void display_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) 
{
   const int32_t x1 = area->x1;
   const int32_t y1 = area->y1;
   const int32_t w  = area->x2 - area->x1 + 1;
   const int32_t h  = area->y2 - area->y1 + 1;

         // lv_color_format_t cf = lv_display_get_color_format(disp_drv);
         // uint8_t bytespp = lv_color_format_get_size(cf);
         // Serial.printf("flush: cf=%d bytespp=%u\n", (int)cf, (unsigned)bytespp);

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
 * LVGL callback to read touch screen 
 */
bool touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) 
{
   touch_point_t points[2];

   uint8_t touches = readTouchScreen((touch_point_t *)&points, SCREEN_ROTATION);
   if(touches > 0) {
         // Serial.printf("Touch: (%d, %d)\n", touchX, touchY);
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
   // Initialize LCD GPIO's
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off   
   vTaskDelay(100);
 
   // Configure TFT backlight dimming PWM
   ledcSetup(BKLT_CHANNEL, BKLT_FREQ, BKLT_RESOLUTION);
   ledcAttachPin(PIN_LCD_BKLT, BKLT_CHANNEL);  // attach the channel to GPIO pin to control dimming      
   setBacklight(255);                     // full brightness

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

   // Init LVGL graphics engine
   lv_init();
   lv_log_register_print_cb(my_lvgl_log_cb); // dump logging to serial console

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
   size_t buf_pixels = (size_t)SCREEN_WIDTH * PSRAM_LINES;
   size_t buf_bytes  = buf_pixels * sizeof(uint16_t);

   lv_buf1_psram = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   lv_buf2_psram = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

   // Supply buffers to LVGL (double-buffered, partial rendering)
   lv_display_set_buffers(main_disp,
         lv_buf1_psram, lv_buf2_psram,
         buf_bytes,
         LV_DISPLAY_RENDER_MODE_PARTIAL);

   // Allocate a small DMA-capable SRAM bounce buffer for speed.
   size_t sram_pixels = SCREEN_WIDTH * SRAM_LINES;
   size_t sram_bytes  = sram_pixels * sizeof(uint16_t);
   sram_linebuf = (uint16_t *)heap_caps_malloc(sram_bytes,
         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);         
   
   // Get the active screen object for this display
   lv_obj_t *scr = lv_display_get_screen_active(main_disp);
   // Set background color and opacity
   lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN); // plain old black
   lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 100);   // not opaque

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
void pageBuilder(void)
{
   tileview = lv_tileview_create(lv_screen_active());
   lv_obj_add_style(tileview, &style_tileview, LV_PART_MAIN);
   lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
   lv_obj_add_event_cb(tileview, tileview_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

   // Create tiles
   tv_demo_page1 =  lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
   // tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
   // tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
   // tv_demo_page4 =  lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
   // tv_demo_page5 =  lv_tileview_add_tile(tileview, 4, 0, LV_DIR_LEFT);   

   // build tile pages
   demo_page1(tv_demo_page1);             // build the top level switch page
   // demo_page2(tv_demo_page2);
   // demo_page3(tv_demo_page3);
   // demo_page4(tv_demo_page4);
   // demo_page5(tv_demo_page5);

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_OFF);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   // show demo info
   lv_obj_t *scrn1_demo_label = lv_label_create(parent);
   lv_obj_set_size(scrn1_demo_label, 320, 40);
   lv_obj_add_style(scrn1_demo_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1_demo_label, 0, LV_PART_MAIN);
   lv_label_set_text(scrn1_demo_label, "WATT-IZ GIF DEMO");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 5);      

   const char gif[] = {"/sample_200.gif"};
   
   if(sd.fexists(gif)) {
      Serial.println("Gif File OK");
   } else 
      Serial.println("Gif File not found!");

   lv_obj_t *img = lv_gif_create(parent);
   lv_gif_set_color_format(img, LV_COLOR_FORMAT_ARGB8888);
   /* Assuming a File system is attached to letter 'A'
   * E.g. set LV_USE_FS_STDIO 'A' in lv_conf.h */
   lv_gif_set_src(img, gif);
   int32_t lpcnt = lv_gif_get_loop_count(img);
   Serial.printf("gif loop count=%d\n", lpcnt);
   lv_obj_align(img, LV_ALIGN_BOTTOM_MID, 0, -10);

   // Swipe label
   // lv_obj_t *swipe_label = lv_label_create(parent);
   // lv_obj_set_size(swipe_label, 120, 36);
   // lv_obj_center(swipe_label);
   // lv_label_set_text(swipe_label, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   // lv_obj_set_style_text_color(swipe_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   // lv_obj_set_style_text_align(swipe_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   // lv_obj_set_style_text_font(swipe_label, &lv_font_montserrat_20, LV_PART_MAIN);   
   // lv_obj_align(swipe_label, LV_ALIGN_BOTTOM_MID, 0, 0);   
  
}


/********************************************************************
 * Demo Page 2
 */
void demo_page2(lv_obj_t *parent)
{
   
}


/********************************************************************
 * Demo page 3 - Drag & Drop
 */
void demo_page3(lv_obj_t *parent)
{
   
}


/********************************************************************
 * @brief Demo Page 4 - Display a QR code. 
 * @NOTE: To support QR code, do the following in lv_conf.h:
 * Set option "LV_USE_QRCODE" to 1
 */
void demo_page4(lv_obj_t *parent)
{
   
}


/********************************************************************
 * @brief Demo Page 5 - Calibration for resistive touch screens.
 */
void demo_page5(lv_obj_t *parent)
{ 

}   


/********************************************************************
 * @brief Helper function to draw an arrow in one of four corners.
 */
void displayCornerCarot(uint8_t corner)
{
   switch(corner) {
      case 0:
         lv_line_set_points(line1, UL_line_points, 6);     /*Set the points*/
         lv_obj_align(line1, LV_ALIGN_TOP_LEFT, -12, -12); 
         break;

      case 1:
         lv_line_set_points(line1, LL_line_points, 6);     /*Set the points*/      
         lv_obj_align(line1, LV_ALIGN_BOTTOM_LEFT, -12, 12); 
         break;   
         
      case 2:
         lv_line_set_points(line1, UR_line_points, 6);     /*Set the points*/      
         lv_obj_align(line1, LV_ALIGN_TOP_RIGHT, 12, -12);   
         break;

      case 3:
         lv_line_set_points(line1, LR_line_points, 6);     /*Set the points*/      
         lv_obj_align(line1, LV_ALIGN_BOTTOM_RIGHT, 12, 12); 
         break;

      default:
         lv_line_set_points(line1, END_line_points, 2);     /*Set the points*/      
         lv_obj_align(line1, LV_ALIGN_BOTTOM_RIGHT, 12, 12);  
         break;
   }
   lv_refr_now(NULL);
   lv_timer_handler();  
}


/********************************************************************
 * @brief Switch event handler
 */
static void roller_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * obj = lv_event_get_target_obj(e);
   if(code == LV_EVENT_VALUE_CHANGED) {
      char buf[32];
      lv_roller_get_selected_str(obj, buf, sizeof(buf));
      Serial.printf("Selected month: %s\n", buf);
   }
}


/********************************************************************
 * @brief Switch event handler
 */
static void switch_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * obj = lv_event_get_target_obj(e);

   if(code == LV_EVENT_VALUE_CHANGED) {
      if(lv_obj_has_state(obj, LV_STATE_CHECKED))
         lv_label_set_text(demo_switch_label, "ON");
      else 
         lv_label_set_text(demo_switch_label, "OFF");
   }
}


/********************************************************************
 * @brief Slider event handler
 */
static void slider_event_cb(lv_event_t * e)
{
   lv_obj_t * slider = lv_event_get_target_obj(e);
   char buf[8];
   lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(slider));
   lv_label_set_text(slider_label, buf);
   //  lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

/********************************************************************
 * Drag & Drop event callback
 */
static void drag_abs_cb(lv_event_t * e) 
{
   lv_event_code_t code = lv_event_get_code(e);    // event type (click, press, etc.)
   lv_obj_t * obj       = (lv_obj_t *)lv_event_get_target(e); // object to drag
   lv_obj_t * parent    = lv_obj_get_parent(obj);  // parent screen
   drag_ctx_t * ctx     = (drag_ctx_t *)lv_event_get_user_data(e); // drag struct passed during obj creation
   lv_indev_t * indev   = lv_event_get_indev(e);   // input device 
   if(!parent || !indev) return;          // exit if no parent screen or input device

   lv_coord_t delta_x, delta_y;           // calc movement 
   lv_coord_t nx, ny;                     // new position

   // Get finger position in SCREEN coords
   lv_point_t p_scr;
   lv_indev_get_point(indev, &p_scr);

   // Parent SCREEN rect and current content scroll
   lv_area_t scr_area;
   lv_obj_get_coords(parent, &scr_area);

   // height & width of drag object
   int32_t obj_W = lv_obj_get_width(obj);
   int32_t obj_H = lv_obj_get_height(obj);

   // Left & top screen boundries
   const lv_coord_t pcxL = 0 - ((scr_area.x1 + scr_area.x2) / 2);   // x min
   const lv_coord_t pcyT = 0 - ((scr_area.y1 + scr_area.y2) / 2);   // y min

   // process event code
   switch(code) {
      case LV_EVENT_PRESSED:
         // prevent parent screen from scrolling
         lv_obj_set_flag(tileview, LV_OBJ_FLAG_SCROLLABLE, false);

         // Store snapshot of initial screen touch position and drag object starting position
         ctx->touch_pos.x = p_scr.x;         // starting touch point relative to screen UL corner coord
         ctx->touch_pos.y = p_scr.y; 
         ctx->drag_obj_pos.x = lv_obj_get_x(obj);  // starting drag obj point relative to obj UL corner
         ctx->drag_obj_pos.y = lv_obj_get_y(obj);      
         break;

      case LV_EVENT_PRESSING:
         delta_x = p_scr.x - ctx->touch_pos.x;      // calc movement on x axis
         delta_y = p_scr.y - ctx->touch_pos.y;      // calc movement on x axis      
         // Calc new drag position for X
         nx = ctx->drag_obj_pos.x + (obj_W/2) + delta_x; // obj left side + movement 
         // Constrain drag object inside X screen boundaries
         if(nx < obj_W/2) nx = obj_W/2;
         if(nx > scr_area.x2 - (obj_W/2)) nx = scr_area.x2 - (obj_W/2);
         // Calc new object position
         ny = ctx->drag_obj_pos.y + (obj_H/2) + delta_y; // obj top side + movement 
         // Constrain drag object inside Y screen boundaries
         if(ny < obj_H/2) ny = obj_H/2;
         if(ny > scr_area.y2 - (obj_H/2)) ny = scr_area.y2 - (obj_H/2);      
         // Set new object position
         lv_obj_set_pos(obj, pcxL + nx, pcyT + ny);
         break;

      case LV_EVENT_RELEASED:             // end of drag operation
         if(ctx->parent_scrollable) {
            lv_obj_set_flag(tileview, LV_OBJ_FLAG_SCROLLABLE, true); // restore parent scroll 
         }
         if(ctx->drop_cb) {               // call user drop function if not NULL
            ctx->drop_cb();
         }
         break;
   }
}


/********************************************************************
 * User drag/drop handler callback
 */
void drop_handler_cb(void)
{
   char buf[30];
   lv_coord_t x = lv_obj_get_x(demo_butn2);
   lv_coord_t y = lv_obj_get_y(demo_butn2);
   Serial.printf("Dropped @ x=%d, y=%d\n", x, y);  // report drop loc on console
   sprintf(buf, "DRAG ME!\nX:%03d Y:%03d", x, y);
   lv_label_set_text(drag_drop_label, buf);
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click or ? event.
*/
void butn_event_cb(lv_event_t * e)
{
   uint16_t i;
   uint32_t x, y, tmo;

   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(butn == demo_butn1) {
      uint8_t flag = get_label_flag(demo_label1);
      flag = flag ^ 1;
      set_label_flag(demo_label1, flag);
      if(flag > 0) {
         lv_obj_set_style_bg_color(demo_label1, lv_color_hex(0xFF0000), LV_PART_MAIN);
      } else {
         lv_obj_set_style_bg_color(demo_label1, lv_color_hex(0x0000FF), LV_PART_MAIN);
      }
         // touch_calibrate();
   } 
#if defined(USE_RESISTIVE_TOUCH_SCREEN)   
   else if(butn == touch_cal_butn) {
      lv_obj_add_state(touch_cal_butn, LV_STATE_DISABLED);  // disable calib butn 
      next_calib_state = 0;
      // wait for butn released for 100 ms 
      tmo = millis();
      while(millis() - tmo < 100) {
         if(gfx.getTouchRaw(&x, &y) > 0)
            tmo = millis();
      }   
      lv_refr_now(NULL);
      lv_timer_handler();  
   }
#endif   
}


/********************************************************************
 * Store a uint8_t value in object user_data
 */
static inline void set_label_flag(lv_obj_t *obj, uint8_t value) 
{
   lv_obj_set_user_data(obj, (void*)(uintptr_t)value);
}


/********************************************************************
 * Retrieve a uint8_t value from object user_data
 */
static inline uint8_t get_label_flag(lv_obj_t *obj) 
{
   return (uint8_t)(uintptr_t)lv_obj_get_user_data(obj);
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
   lv_style_set_border_color(&style_butn_released, lv_palette_darken(LV_PALETTE_PINK, 1));
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

