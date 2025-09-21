/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  
// lv_coord_t * fft_bufr;
// #define DISPLAY_POINTS           512

// Graphics hardware library
TFT_eSPI tft = TFT_eSPI();
#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   uint16_t calData[5] = { 360, 3400, 360, 3400, 7 }; // Example — use your own from calibration
#endif

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib

int16_t gui_event;


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
   const uint16_t *src = (const uint16_t*)px_map;

   tft.startWrite();
   for (int32_t row = 0; row < h; ) {
      int32_t lines = LINES_PER_CHUNK;
      if (row + lines > h) lines = h - row;

      // Copy from LVGL buffer (PSRAM) to small internal RAM buffer
      memcpy(dma_linebuf, &src[row * w], w * lines * sizeof(uint16_t));

      // Push this window
      tft.setAddrWindow(x1, y1 + row, w, lines);
      // NOTE: Do NOT use DMA API on S3; just regular pushPixels works everywhere.
      tft.pushPixels(dma_linebuf, w * lines);

      row += lines;
   }
   tft.endWrite();   

   lv_disp_flush_ready(disp_drv);
}


/********************************************************************
 * Callback to read touch screen 
 */
bool touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) 
{
#if defined(USE_CAPACITIVE_TOUCH_SCREEN)   
   touch_point_t points[2];               // room for two touch points
   // Capacitive touch I2C function 
   uint8_t touches = readFT6336(points, 2, tft.getRotation());

   if(touches > 0) {
      data->point.x = points[0].x;
      data->point.y = points[0].y;
      data->state = LV_INDEV_STATE_PRESSED;
   } else {
      data->state = LV_INDEV_STATE_RELEASED;
   }
   return (touches > 0); // true if screen touched

#elif defined(USE_RESISTIVE_TOUCH_SCREEN)   
   uint16_t touchX, touchY;

   bool touched = tft.getTouch(&touchX, &touchY);

   if(!touched) {
      data->state = LV_INDEV_STATE_REL; // Released
   } else {
      data->state = LV_INDEV_STATE_PR;  // Pressed

      switch(tft.getRotation()) {
         case 0:                          // portrait 
            data->point.x = map(touchY, 0, SCREEN_HEIGHT, 0, SCREEN_WIDTH);
            data->point.y = map(touchX, 0, SCREEN_WIDTH, 0, SCREEN_HEIGHT);
            break;

         case 1:                          // landscape
            data->point.x = touchX;
            data->point.y = SCREEN_HEIGHT - touchY;
            break;

         case 2:
            data->point.x = map(touchY, 0, SCREEN_HEIGHT, SCREEN_WIDTH, 0);
            data->point.y = map(touchX, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
            break;

         case 3:
            data->point.x = SCREEN_WIDTH- touchX;
            data->point.y = touchY;
            break;
      }

      Serial.printf("tx=%d, ty=%d, x=%d, y=%d, rot=%d\n", touchX, touchY, data->point.x, data->point.y, tft.getRotation());
   }
   return touched;
#endif
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
   // Default resistive screen coords - run touch_calibrate() to improve accuracy
   uint16_t touch_calib_data[] = {360, 3600, 300, 3800, SCREEN_ROTATION}; 

   gui_event = GUI_EVENT_NONE;

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

   // Init lvgl & tft libraries
   tft.init(0);                           // low level tft_espi hardware library
   tft.setRotation(SCREEN_ROTATION);      // use landscape orientation (1 or 3)

#if defined(USE_RESISTIVE_TOUCH_SCREEN)  
   if(NVS_OK) {     
      NVS.getBlob("nvs_tcal", (uint8_t *)&touch_calib_data, sizeof(touch_calib_data));

         for(int i=0; i<5; i++) {
            Serial.printf("%d, ", (uint16_t *)touch_calib_data[i]);
         }
         Serial.println("");
   }
   // tft.setTouch(&touch_calib_data[0]);   
#endif
   tft.fillScreen(TFT_BLACK); 
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   tft.setSwapBytes(true);                // swap bytes for lvgl

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


   // Create display buffer
   main_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);

   // Allocate LVGL draw buffers in PSRAM (saves SRAM)
   size_t pix_per_buf = (size_t)SCREEN_WIDTH * ROWS_PER_BUF;
   size_t bytes_per_buf = pix_per_buf * sizeof(lv_color_t);

   buf1 = (lv_color_t*) heap_caps_malloc(bytes_per_buf, MALLOC_CAP_SPIRAM); // | MALLOC_CAP_8BIT);
   buf2 = (lv_color_t*) heap_caps_malloc(bytes_per_buf, MALLOC_CAP_SPIRAM); // | MALLOC_CAP_8BIT);

   // Create small internal bounce buffer in internal SRAM for speed
   dma_linebuf = (uint16_t*) heap_caps_malloc(SCREEN_WIDTH * LINES_PER_CHUNK * sizeof(uint16_t),
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   

   // Set display buffers and the flush callback
   lv_display_set_buffers(main_disp, buf1, buf2, bytes_per_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);  
   lv_display_set_flush_cb(main_disp, display_flush_cb);

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
   tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_NONE);   

   // build tile pages
   demo_page1(tv_demo_page1);             // build each page
   demo_page2(tv_demo_page2);             
   demo_KB_page(tv_demo_page3);
   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_ON);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   LV_IMAGE_DECLARE(chat_gpt_demo_300x150_bw); 

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
   lv_image_set_src(img1, &chat_gpt_demo_300x150_bw);
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
 * Demo Page 2
 */
void demo_page2(lv_obj_t *parent)
{
   // show state of wifi connect
   scrn2_wifi_label = lv_label_create(parent);
   lv_obj_set_size(scrn2_wifi_label, 54, 54);
   lv_obj_set_style_radius(scrn2_wifi_label, 27, LV_PART_MAIN);
   lv_obj_add_style(scrn2_wifi_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn2_wifi_label, 3, LV_PART_MAIN);
   lv_obj_set_style_border_color(scrn2_wifi_label, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(scrn2_wifi_label, LV_SYMBOL_WIFI);
   lv_obj_set_style_text_align(scrn2_wifi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_pad_top(scrn2_wifi_label, 12, LV_PART_MAIN);
   lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn2_wifi_label, &lv_font_montserrat_28, LV_PART_MAIN);
   lv_obj_align(scrn2_wifi_label, LV_ALIGN_TOP_LEFT, 6, 0);     

   // ASK button. Click to enter keyboard page.
   ask_butn = lv_button_create(parent);
   lv_obj_set_size(ask_butn, 110, 45);
   lv_obj_align(ask_butn, LV_ALIGN_TOP_MID, 0, 5);
   lv_obj_add_style(ask_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(ask_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(ask_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_add_state(ask_butn, LV_STATE_DISABLED);

   ask_butn_label = lv_label_create(ask_butn);
   lv_obj_add_style(ask_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(ask_butn_label, "ASK CHAT");
   lv_obj_center(ask_butn_label);

   ask_request_label = lv_textarea_create(parent);
   lv_obj_set_size(ask_request_label, 310, 65);   
   lv_obj_align_to(ask_request_label, ask_butn, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);    
   lv_obj_set_scroll_dir(ask_request_label, LV_DIR_VER);  // only vertical scrolling     
   // Make it display-only (no editing, no cursor)
   // Keep it scrollable, vertical only 
   lv_obj_set_scroll_dir(ask_request_label, LV_DIR_VER);
   lv_obj_set_scrollbar_mode(ask_request_label, LV_SCROLLBAR_MODE_AUTO);
   lv_obj_set_style_bg_opa(ask_request_label, LV_OPA_TRANSP, LV_PART_CURSOR);
   lv_textarea_set_placeholder_text(ask_request_label, "Request");
   lv_obj_set_style_bg_opa(ask_request_label, LV_OPA_100, LV_PART_MAIN);
   lv_obj_set_style_border_width(ask_request_label, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(ask_request_label, lv_palette_darken(LV_PALETTE_DEEP_ORANGE, 1), LV_PART_MAIN); 
   lv_obj_set_style_bg_color(ask_request_label, lv_palette_darken(LV_PALETTE_BLUE, 1), LV_PART_MAIN);
   lv_obj_set_style_text_color(ask_request_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_text_font(ask_request_label, &lv_font_montserrat_16, LV_PART_MAIN);
   // Make it display-only (no editing, no cursor)
   lv_obj_set_style_opa(ask_request_label, LV_OPA_TRANSP, LV_PART_CURSOR);     // cursor invisible
   lv_textarea_set_text_selection(ask_request_label, false);                    // no select/drag       

   // ASK response textarea
   ask_response_label = lv_textarea_create(parent);
   lv_obj_set_size(ask_response_label, 310, 100);   
   lv_obj_align_to(ask_response_label, ask_request_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);   
   lv_obj_set_scroll_dir(ask_response_label, LV_DIR_VER);  // only vertical scrolling     
   // Keep it scrollable, vertical only 
   lv_obj_set_scroll_dir(ask_response_label, LV_DIR_VER);
   lv_obj_set_scrollbar_mode(ask_response_label, LV_SCROLLBAR_MODE_AUTO);
   lv_obj_set_style_bg_opa(ask_response_label, LV_OPA_TRANSP, LV_PART_CURSOR);
   lv_textarea_set_placeholder_text(ask_response_label, "Response");
   lv_obj_set_style_bg_opa(ask_response_label, LV_OPA_100, LV_PART_MAIN);
   lv_obj_set_style_border_width(ask_response_label, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(ask_response_label, lv_color_white(), LV_PART_MAIN); 
   lv_obj_set_style_bg_color(ask_response_label, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
   lv_obj_set_style_text_color(ask_response_label, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_font(ask_response_label, &lv_font_montserrat_16, LV_PART_MAIN);
   // Make it display-only (no editing, no cursor)
   lv_obj_set_style_opa(ask_response_label, LV_OPA_TRANSP, LV_PART_CURSOR);     // cursor invisible
   lv_textarea_set_text_selection(ask_response_label, false);                    // no select/drag      

   // Timer with lambda function to display wifi connection status
   chat_timer = lv_timer_create([](lv_timer_t *timer) {
      static bool blink = false;
      if(WiFi.status() == WL_CONNECTED) {
         lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_MAIN);
         lv_obj_clear_state(ask_butn, LV_STATE_DISABLED);
      } else {
         blink ^= true;
         lv_obj_add_state(ask_butn, LV_STATE_DISABLED);
         if(blink)
            lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
         else 
            lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
      }
   }, 500, NULL);    
}


/********************************************************************
 * Demo Page 3 - Keyboard entry
 */
void demo_KB_page(lv_obj_t *parent)
{
   // Create a keyboard with a text areas
   keyboard_pop = lv_keyboard_create(parent);
   lv_obj_set_size(keyboard_pop, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 68);
   lv_obj_add_style(keyboard_pop, &style_keyboard, LV_PART_MAIN);
   lv_obj_add_style(keyboard_pop, &style_keyboard_custom, LV_PART_ITEMS | LV_STATE_CHECKED);
   lv_obj_add_event_cb(keyboard_pop, kb_event_cb, LV_EVENT_ALL, NULL);

   // create title label
   keyboard_ta_title = lv_label_create(parent);
   lv_obj_set_size(keyboard_ta_title, 60, 60);
   lv_obj_align(keyboard_ta_title, LV_ALIGN_TOP_LEFT, 2, 10);   
   lv_obj_set_style_text_color(keyboard_ta_title, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_border_width(keyboard_ta_title, 0, LV_PART_MAIN);
   // lv_obj_set_style_border_color(keyboard_ta_title, lv_color_white(), LV_PART_MAIN);   
   lv_obj_set_style_text_font(keyboard_ta_title, &lv_font_montserrat_16, LV_PART_MAIN);   
   lv_label_set_text(keyboard_ta_title, "Chat:");

   // Create a text area. The keyboard will write here
   keyboard_text_area = lv_textarea_create(parent);
   lv_obj_set_size(keyboard_text_area, 256, 60);   
   lv_obj_set_style_bg_color(keyboard_text_area, lv_palette_darken(LV_PALETTE_BLUE, 1), LV_PART_MAIN);
   lv_obj_set_style_text_color(keyboard_text_area, lv_color_white(), LV_PART_MAIN); 
   lv_textarea_set_text(keyboard_text_area, "");
   lv_textarea_set_one_line(keyboard_text_area, false);
   lv_obj_align(keyboard_text_area, LV_ALIGN_TOP_RIGHT, 0, 0);
   lv_obj_set_scrollbar_mode(keyboard_text_area, LV_SCROLLBAR_MODE_AUTO);     // don't show scrollbars on non-scrolling pages
   lv_obj_add_event_cb(keyboard_text_area, ta_event_cb, LV_EVENT_ALL, NULL);   
   lv_obj_set_style_text_font(keyboard_text_area, &lv_font_montserrat_14, LV_PART_MAIN);   
   lv_textarea_set_cursor_pos(keyboard_text_area, LV_TEXTAREA_CURSOR_LAST); // put cursor at end 
   lv_obj_add_state(keyboard_text_area, LV_STATE_FOCUSED);   
   lv_keyboard_set_textarea(keyboard_pop, keyboard_text_area);
}


/********************************************************************
*  @fn Keyboar event handler. Comes here on keyboard click event.
*/
void kb_event_cb(lv_event_t * e)
{
   lv_obj_t * kb  = (lv_obj_t *)lv_event_get_target(e);
   lv_event_code_t code = lv_event_get_code(e);

   if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
      const char * txt = lv_btnmatrix_get_btn_text(kb, lv_btnmatrix_get_selected_btn(kb));

      // If key pressed is RETURN key, save text and exit KB demo page
      if(txt && strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
         lv_textarea_set_text(ask_request_label, lv_textarea_get_text(keyboard_text_area));
         lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON);  
         gui_event = GUI_EVENT_START_CHAT;
      }
   }
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(code == LV_EVENT_CLICKED) {
      if(butn == ask_butn) {      // ask butn clicked - call up keyboard
         lv_textarea_add_text(ask_request_label, "");
         lv_textarea_add_text(ask_response_label, "");
         lv_textarea_set_text(keyboard_text_area, "");         
         lv_obj_set_tile(tileview, tv_demo_page3, LV_ANIM_ON);   // default starting page
      }
   }
}


/********************************************************************
*  @brief Event handler for keyboard text area object
* 
*  @param e - event object
*/
void ta_event_cb(lv_event_t * e)
{
   uint16_t i;
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
   uint32_t key = lv_event_get_key(e);

   if(code == LV_EVENT_CANCEL) {
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON); 
   }
   else if(code == LV_EVENT_READY) {   // Ready key (check key) pressed?
      lv_textarea_set_text(ask_request_label, lv_textarea_get_text(keyboard_text_area));
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_OFF);       
      gui_event = GUI_EVENT_START_CHAT;            
   } 
}


/********************************************************************
 * @brief Get keyboard text from the request label
 */
const char * getKeyboardText(void)
{
   const char *txt;
   txt = lv_textarea_get_text(ask_request_label);
   return txt;
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
*  @brief Put chat response text into response label
*/
bool setChatResponseText(String input)
{
   lv_textarea_add_text(ask_response_label, input.c_str());
   return true;
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

   /**
   *  @brief Init the default style of the keyboard
   */
   lv_style_init(&style_keyboard);
   lv_style_set_bg_color(&style_keyboard, lv_color_black());
   lv_style_set_border_width(&style_keyboard, 0);
   lv_style_set_text_color(&style_keyboard, lv_color_white());
   lv_style_set_text_font(&style_keyboard, &lv_font_montserrat_18);
   lv_style_set_pad_all(&style_keyboard, 0);
   lv_style_set_opa(&style_keyboard, LV_OPA_COVER);

   /**
   *  @brief Init the custom keyboard style
   */
   lv_style_init(&style_keyboard_custom);
   lv_style_set_bg_color(&style_keyboard_custom, lv_palette_lighten(LV_PALETTE_BLUE, 1));
   lv_style_set_border_width(&style_keyboard_custom, 0);
   lv_style_set_text_color(&style_keyboard_custom, lv_color_white());
   lv_style_set_text_font(&style_keyboard_custom, &lv_font_montserrat_18);
   lv_style_set_pad_all(&style_keyboard_custom, 0);
   lv_style_set_opa(&style_keyboard_custom, LV_OPA_COVER);      
}


/********************************************************************
 * Return X/Y coords from a capacitive touch panel using a FT6336 controller
 */
uint8_t readFT6336(touch_point_t *points, uint8_t maxPoints, uint8_t scrn_rotate) 
{
#define MAX_POINTS   13
   uint16_t px, py;

   Wire.beginTransmission(FT6336_ADDR);
   Wire.write(0x02); // Start at Touch Points register
   if (Wire.endTransmission(false) != 0) return 0;

   // Request 1 byte for count + 12 bytes for 2 points
   Wire.requestFrom(FT6336_ADDR, MAX_POINTS);
   if (Wire.available() < 7) return 0;    // must have at least 7 bytes to return

   uint8_t touches = Wire.read(); // Number of active touches (max 2)
   if(touches > maxPoints) touches = maxPoints;

   for (uint8_t i = 0; i < touches; i++) {
      uint8_t xh = Wire.read();  // XH
      uint8_t xl = Wire.read();  // XL
      uint8_t yh = Wire.read();  // YH
      uint8_t yl = Wire.read();  // YL
      px = ((xh & 0x0F) << 8) | xl;
      py = ((yh & 0x0F) << 8) | yl;
      Wire.read();               // Weight (skip)
      Wire.read();               // Area (skip)

      // Correct X/Y coordinates depending on screen rotation
      switch(scrn_rotate) {
         case 0:
            points[i].x = px;
            points[i].y = py;
            break;

         case 1:
            points[i].y = SCREEN_HEIGHT - px;
            points[i].x = py;
            break;

         case 2:
            points[i].x = SCREEN_WIDTH - px;
            points[i].y = SCREEN_HEIGHT - py;
            break;
         
         case 3:
            points[i].x = SCREEN_WIDTH - py;         
            points[i].y = px;      
            break;
      }
   }
   return touches;
}

