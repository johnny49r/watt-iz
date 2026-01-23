/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
uint16_t msgBoxBtnTag = MBOX_BTN_NONE;
int16_t gui_event;

LV_IMAGE_DECLARE(text_to_speech_icon_300x150_bw_inverted);

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
   gui_event = GUI_EVENT_NONE;

   // Initialize LCD GPIO's
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off   
   vTaskDelay(100);
 
   // Configure TFT backlight dimming PWM
   ledcSetup(BKLT_CHANNEL, BKLT_FREQ, BKLT_RESOLUTION);
   ledcAttachPin(PIN_LCD_BKLT, BKLT_CHANNEL);  // attach the channel to GPIO pin to control dimming      
   setBacklight(0);                     // full brightness

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
   tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT);

   // build tile pages
   demo_page1(tv_demo_page1);             // build the top level switch page
   demo_page2(tv_demo_page2);             // build the top level switch page   

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
   lv_label_set_text(scrn1_demo_label, "WATT-IZ DEMO PROJECTS");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 5);      

   // project image
   lv_obj_t * img1 = lv_image_create(parent);
   lv_image_set_src(img1, &text_to_speech_icon_300x150_bw_inverted);
   lv_obj_align_to(img1, scrn1_demo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5); 

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
 * Demo Page 2
 */
void demo_page2(lv_obj_t *parent)
{  
   // show state of wifi connect
   scrn2_wifi_label = lv_label_create(parent);
   lv_obj_set_size(scrn2_wifi_label, 50, 50);
   lv_obj_set_style_radius(scrn2_wifi_label, 25, LV_PART_MAIN);
   lv_obj_add_style(scrn2_wifi_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn2_wifi_label, 2, LV_PART_MAIN);
   lv_obj_set_style_border_color(scrn2_wifi_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);   
   lv_label_set_text(scrn2_wifi_label, LV_SYMBOL_WIFI);
   lv_obj_set_style_text_align(scrn2_wifi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_pad_top(scrn2_wifi_label, 9, LV_PART_MAIN);
   lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn2_wifi_label, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_align(scrn2_wifi_label, LV_ALIGN_TOP_LEFT, 6, 3);     

   // talk button
   talk_butn = lv_button_create(parent);
   lv_obj_set_size(talk_butn, 80, 45);
   lv_obj_align(talk_butn, LV_ALIGN_TOP_MID, 0, 10);
   lv_obj_add_style(talk_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(talk_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(talk_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_add_state(talk_butn, LV_STATE_DISABLED);

   lv_obj_t *talk_butn_label = lv_label_create(talk_butn);
   lv_obj_add_style(talk_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(talk_butn_label, "Play!");
   lv_obj_center(talk_butn_label);

   talk_text_label = lv_label_create(parent);
   lv_obj_set_size(talk_text_label, 316, 160);
   lv_obj_add_style(talk_text_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(talk_text_label, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(talk_text_label, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(talk_text_label, "");
   lv_obj_align(talk_text_label, LV_ALIGN_BOTTOM_MID, 0, -5); 

   // timer with lambda function to display wifi connection status
   talk_timer = lv_timer_create([](lv_timer_t *timer) {
      static bool blink = false;
      if(WiFi.status() == WL_CONNECTED) {
         lv_obj_set_style_text_color(scrn2_wifi_label, lv_color_white(), LV_PART_MAIN);
         lv_obj_clear_state(talk_butn, LV_STATE_DISABLED);
      } else {
         blink ^= true;
         lv_obj_add_state(talk_butn, LV_STATE_DISABLED);
         if(blink)
            lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
         else 
            lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
      }
   }, 500, NULL);    
}


/********************************************************************
 * Load text into talk text label
 */
void setLabelText(char *labtext)
{
   lv_label_set_text(talk_text_label, labtext);
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(code == LV_EVENT_CLICKED) {
      if(butn == talk_butn) {
         gui_event = GUI_EVENT_TALK;
      }

      if(butn == next_butn) {
         lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON); 
      }
   }
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
   lv_style_set_bg_color(&style_butn_released, lv_palette_darken(LV_PALETTE_BLUE, 1));
   lv_style_set_border_width(&style_butn_released, 1);
   lv_style_set_border_color(&style_butn_released, lv_palette_lighten(LV_PALETTE_PINK, 1));
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
   lv_style_set_radius(&style_label_default, 5);
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