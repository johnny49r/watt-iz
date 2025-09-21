/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

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
   /*Create a normal drop down list*/
   ddl = lv_dropdown_create(parent);
   lv_obj_set_size(ddl, 200, 32);   
   lv_obj_set_style_bg_color(ddl, lv_palette_lighten(LV_PALETTE_PINK, 1), LV_PART_MAIN);
   lv_obj_set_style_border_color(ddl, lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 1), LV_PART_MAIN);
   lv_obj_set_style_text_color(ddl, lv_color_white(), LV_PART_MAIN);
   lv_dropdown_set_options(ddl, "Card Type: ?\n"
                           "Capacity: ?\n"
                           "Available: ?\n"
                           "Used: ?");

   lv_obj_align(ddl, LV_ALIGN_TOP_MID, 0, 12);
   lv_obj_add_event_cb(ddl, dd_event_handler, LV_EVENT_ALL, NULL);

   file_list = lv_list_create(parent);
   lv_obj_set_size(file_list, 310, 185);   
   lv_obj_set_style_radius(file_list, 6, LV_PART_MAIN);
   lv_obj_set_style_bg_color(file_list, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);  
   lv_obj_align_to(file_list, ddl, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
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

   /**
    * @brief Create a write speed test circular meter 
    */
   write_meter = lv_scale_create(parent);
   lv_obj_set_size(write_meter, 135, 135);
   lv_obj_align(write_meter, LV_ALIGN_LEFT_MID, 5, -10);

   // Make it circular and define range
   lv_obj_set_style_bg_opa(write_meter, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(write_meter, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_radius(write_meter, LV_RADIUS_CIRCLE, LV_PART_MAIN);
   lv_obj_set_style_clip_corner(write_meter, true, LV_PART_MAIN);   
   lv_scale_set_mode(write_meter, LV_SCALE_MODE_ROUND_INNER);
   lv_obj_set_style_outline_width(write_meter, 5, LV_PART_MAIN);
   lv_obj_set_style_outline_color(write_meter, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);   
   lv_obj_set_style_pad_all(write_meter, 0, LV_PART_MAIN);

   lv_scale_set_range(write_meter, 0, 50);   
   lv_scale_set_total_tick_count(write_meter, 51);
   lv_scale_set_major_tick_every(write_meter, 5);

   lv_obj_set_style_length(write_meter, 5, LV_PART_ITEMS);
   lv_obj_set_style_length(write_meter, 10, LV_PART_INDICATOR);

   lv_scale_set_angle_range(write_meter, 270);  // angular range of needle swing
   lv_scale_set_rotation(write_meter, 135);     // angle where 0 starts

   // Show labels as 0..10 (avoid float rounding issues)
   lv_scale_set_label_show(write_meter, true);
   lv_scale_set_text_src(write_meter, labels);   

   lv_obj_t * led1  = lv_led_create(write_meter);
   lv_obj_set_size(led1, 12, 12);
   lv_led_set_color(led1, lv_palette_main(LV_PALETTE_RED));   
   lv_obj_align(led1, LV_ALIGN_CENTER, 0, 0);
   lv_led_on(led1);

   write_needle_line = lv_line_create(write_meter);

   lv_obj_set_style_line_width(write_needle_line, 4, LV_PART_MAIN);
   lv_obj_set_style_line_rounded(write_needle_line, true, LV_PART_MAIN);
   set_write_needle_line_value(write_meter, 0);

   // write speed meter label shows speed on meter face
   write_speed_label = lv_label_create(write_meter);
   lv_obj_set_size(write_speed_label, 45, 45);
   lv_obj_set_style_radius(write_speed_label, 10, LV_PART_MAIN);
   lv_label_set_text(write_speed_label, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   lv_obj_set_style_pad_top(write_speed_label, 4, LV_PART_MAIN);   
   lv_obj_set_style_bg_opa(write_speed_label, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(write_speed_label, lv_color_black(), LV_PART_MAIN);   
   lv_obj_set_style_text_color(write_speed_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_text_align(write_speed_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(write_speed_label, &lv_font_montserrat_14, LV_PART_MAIN);   
   lv_label_set_text(write_speed_label, "00.00\nMB/s");
   lv_obj_align(write_speed_label, LV_ALIGN_BOTTOM_MID, 0, 5);  

   // write meter label - static label under meter
   lv_obj_t *wmeter_label = lv_label_create(parent);
   lv_obj_set_size(wmeter_label, 100, 30);
   lv_obj_add_style(wmeter_label, &style_label_default, LV_PART_MAIN | LV_STATE_DEFAULT);   
   lv_label_set_text(wmeter_label, "WRITING");
   lv_obj_set_style_text_align(wmeter_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_align_to(wmeter_label, write_meter, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);   

   /**
    * @brief Create a read speed test circular meter 
    */
   read_meter = lv_scale_create(parent);
   lv_obj_set_size(read_meter, 135, 135);
   lv_obj_align(read_meter, LV_ALIGN_RIGHT_MID, -5, -10);

   // Make it circular and define range
   lv_obj_set_style_bg_opa(read_meter, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(read_meter, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_radius(read_meter, LV_RADIUS_CIRCLE, LV_PART_MAIN);
   lv_obj_set_style_clip_corner(read_meter, true, LV_PART_MAIN);   
   lv_scale_set_mode(read_meter, LV_SCALE_MODE_ROUND_INNER);
   lv_obj_set_style_outline_width(read_meter, 5, LV_PART_MAIN);
   lv_obj_set_style_outline_color(read_meter, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);  
   lv_obj_set_style_pad_all(read_meter, 0, LV_PART_MAIN);

   lv_scale_set_range(read_meter, 0, 50);   
   lv_scale_set_total_tick_count(read_meter, 51);
   lv_scale_set_major_tick_every(read_meter, 5);

   lv_obj_set_style_length(read_meter, 5, LV_PART_ITEMS);
   lv_obj_set_style_length(read_meter, 10, LV_PART_INDICATOR);

   lv_scale_set_angle_range(read_meter, 270);  // angular range of needle swing
   lv_scale_set_rotation(read_meter, 135);     // angle where 0 starts

   // Show labels as 0..10 (avoid float rounding issues)
   lv_scale_set_label_show(read_meter, true);
   lv_scale_set_text_src(read_meter, labels);   

   lv_obj_t * led2  = lv_led_create(read_meter);
   lv_obj_set_size(led2, 12, 12);
   lv_led_set_color(led2, lv_palette_main(LV_PALETTE_RED));   
   lv_obj_align(led2, LV_ALIGN_CENTER, 0, 0);
   lv_led_on(led2);   

   read_needle_line = lv_line_create(read_meter);

   lv_obj_set_style_line_width(read_needle_line, 4, LV_PART_MAIN);
   lv_obj_set_style_line_rounded(read_needle_line, true, LV_PART_MAIN);
   set_read_needle_line_value(read_meter, 0);

   // read speed meter label - shows speed on meter face
   read_speed_label = lv_label_create(read_meter);
   lv_obj_set_size(read_speed_label, 45, 45);
   lv_obj_set_style_radius(read_speed_label, 10, LV_PART_MAIN);
   lv_label_set_text(read_speed_label, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   lv_obj_set_style_bg_opa(read_speed_label, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(read_speed_label, lv_color_black(), LV_PART_MAIN);   
   lv_obj_set_style_text_color(read_speed_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_text_align(read_speed_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(read_speed_label, &lv_font_montserrat_14, LV_PART_MAIN);   
   lv_label_set_text(read_speed_label, "00.00\nMb/s");
   lv_obj_align(read_speed_label, LV_ALIGN_BOTTOM_MID, 0, 5);  

   // read meter label - static label under meter
   lv_obj_t *rmeter_label = lv_label_create(parent);
   lv_obj_set_size(rmeter_label, 100, 30);
   lv_obj_add_style(rmeter_label, &style_label_default, LV_PART_MAIN | LV_STATE_DEFAULT);   
   lv_label_set_text(rmeter_label, "READING");
   lv_obj_set_style_text_align(rmeter_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);     
   lv_obj_align_to(rmeter_label, read_meter, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);     

   // Start test button
   start_butn = lv_btn_create(parent);
   lv_obj_set_size(start_butn, 74, 36);
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
 * @brief Set needle on WRITE meter.
 */
void set_write_needle_line_value(void * obj, int32_t v)
{
    lv_scale_set_line_needle_value((lv_obj_t *)obj, write_needle_line, 56, v);
}


/********************************************************************
 * @brief Set needle on READ meter.
 */
void set_read_needle_line_value(void * obj, int32_t v)
{
    lv_scale_set_line_needle_value((lv_obj_t *)obj, read_needle_line, 56, v);
}


/********************************************************************
 * @brief Calculate WRITE speed values
 */
void updateWriteSpeed(float mbs)
{
   char buf[16];
   int32_t needle_val;

   sprintf(buf, "%2.2f\nMB/s", mbs);
   lv_label_set_text(write_speed_label, buf);
   needle_val = int(round(mbs * 5.0));       // scale to meter range
   set_write_needle_line_value(write_meter, needle_val);
   lv_obj_invalidate(write_meter);      // the lv_scale (dial)
}


/********************************************************************
 * @brief Calculate READ speed values
 */
void updateReadSpeed(float mbs)
{
   char buf[16];
   int32_t needle_val;

   sprintf(buf, "%2.2f\nMB/s", mbs);
   lv_label_set_text(read_speed_label, buf);
   needle_val = int(round(mbs * 5.0));       // scale to meter range
   set_read_needle_line_value(read_meter, needle_val);
   lv_obj_invalidate(read_meter);      // the lv_scale (dial)   
}


/********************************************************************
 * @brief Update the file dropdown list
 */
void updateDDList(char *list_items)
{
   lv_dropdown_set_options(ddl, list_items) ;
}


/********************************************************************
 * @brief Add new File to the file list
 */
void fileListAddFile(String filename) 
{
   lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_FILE, filename.c_str());
   lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN);   
   lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);  
}


/********************************************************************
 * @brief Add new Directory to the file list
 */
void fileListAddDir(String dirname)
{
   lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_RIGHT, dirname.c_str()); 
   lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
   lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_BLUE, 3), LV_PART_MAIN);   
}


/********************************************************************
 * DropDown widget event handler
 */
static void dd_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target_obj(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        char buf[32];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        LV_LOG_USER("Option: %s", buf);
        lv_dropdown_set_selected(obj, 0);
    }
}


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

