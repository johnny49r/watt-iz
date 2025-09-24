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
   touch_point_t points[2];
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

   // Set default screen orientation
   tft.setRotation(SCREEN_ROTATION);      // display uses landscape orientation (1 or 3)

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
   tft.setSwapBytes(true);                // *** IMPORTANT swap rgb color bytes for lvgl 

   // Init LVGL
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
void pageBuilder(void)
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
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_OFF);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   LV_IMAGE_DECLARE(graphics_demo_icon_300x150_bw);
   
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
   lv_image_set_src(img1, &graphics_demo_icon_300x150_bw);
   lv_obj_align_to(img1, scrn1_demo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5); 

   // Swipe label
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
#if defined(USE_RESISTIVE_TOUCH_SCREEN)   
   touch_cal_butn = lv_btn_create(parent);
   lv_obj_set_size(touch_cal_butn, 80, 50);
   lv_obj_add_style(touch_cal_butn, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(touch_cal_butn, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);   
   lv_obj_set_style_radius(touch_cal_butn, 6, LV_PART_MAIN);   // make button a circle
   lv_obj_align(touch_cal_butn, LV_ALIGN_TOP_LEFT, 0, 0);   
   lv_obj_add_event_cb(touch_cal_butn, &butn_event_cb, LV_EVENT_CLICKED, NULL);  

   // Add a label the button
   lv_obj_t * tc_label = lv_label_create(touch_cal_butn);
   lv_label_set_text(tc_label, "Touch\nCalib");
   lv_obj_add_style(tc_label, &style_label_default, LV_PART_MAIN);  
   lv_obj_center(tc_label);   
#endif

   /*Create a slider in the center of the display*/
   demo_slider = lv_slider_create(parent);
   lv_obj_set_width(demo_slider, 260); 
   lv_obj_align(demo_slider, LV_ALIGN_TOP_MID, 0, 35);   
   lv_obj_add_event_cb(demo_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
   lv_obj_set_style_bg_color(demo_slider, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);   
   lv_obj_set_style_bg_opa(demo_slider, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_bg_color(demo_slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
   lv_obj_set_style_pad_all(demo_slider, 8, LV_PART_KNOB);  // Make the knob larger
   // lv_obj_set_style_radius(demo_slider, 6, LV_PART_KNOB);  
   lv_obj_set_ext_click_area(demo_slider, 15);
   lv_obj_set_style_anim_duration(demo_slider, 1000, 0);
   lv_slider_set_range(demo_slider, 0, 100);   
   lv_slider_set_value(demo_slider, 50, LV_ANIM_ON);

   // Create a label below the slider
   slider_label = lv_label_create(parent);
   lv_obj_set_size(slider_label, 60, 30);    
   lv_obj_add_style(slider_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_align(slider_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); 
   lv_label_set_text(slider_label, "50%");
   lv_obj_align_to(slider_label, demo_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

   // Switch widget demo
   demo_sw = lv_switch_create(parent);
   lv_obj_add_event_cb(demo_sw, switch_event_cb, LV_EVENT_ALL, NULL);
   lv_obj_clear_state(demo_sw, LV_STATE_CHECKED);
   lv_switch_set_orientation(demo_sw, LV_SWITCH_ORIENTATION_HORIZONTAL);   // horiz/vert
   lv_obj_align(demo_sw, LV_ALIGN_CENTER, 0, 0);    

   demo_switch_label = lv_label_create(parent);
   lv_obj_set_size(demo_switch_label, 80, 30);   
   lv_obj_add_style(demo_switch_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(demo_switch_label, "OFF");
   lv_obj_set_style_text_align(demo_switch_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align_to(demo_switch_label, demo_sw, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);   

   demo_butn1 = lv_btn_create(parent);
   lv_obj_set_size(demo_butn1, 50, 50);
   lv_obj_add_style(demo_butn1, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(demo_butn1, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);   
   lv_obj_set_style_radius(demo_butn1, 25, LV_PART_MAIN);   // make button a circle
   lv_obj_align(demo_butn1, LV_ALIGN_LEFT_MID, 20, 0);   
   lv_obj_add_event_cb(demo_butn1, &butn_event_cb, LV_EVENT_CLICKED, NULL);    

   // Create a test label
   demo_label1 = lv_label_create(parent);
   lv_obj_set_size(demo_label1, 80, 30);
   lv_obj_add_style(demo_label1, &style_label_default, LV_PART_MAIN | LV_STATE_DEFAULT);   
   lv_label_set_text(demo_label1, "Press!");
   lv_obj_set_style_bg_opa(demo_label1, LV_OPA_100, LV_PART_MAIN);
   lv_obj_set_style_border_width(demo_label1, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(demo_label1, lv_color_white(), LV_PART_MAIN);   
   lv_obj_set_style_text_align(demo_label1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);  
   lv_obj_align_to(demo_label1, demo_butn1, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
   set_label_flag(demo_label1, 0);     // paint label bg

   demo_roller = lv_roller_create(parent);
   lv_roller_set_options(demo_roller,
                        "January\n"
                        "February\n"
                        "March\n"
                        "April\n"
                        "May\n"
                        "June\n"
                        "July\n"
                        "August\n"
                        "September\n"
                        "October\n"
                        "November\n"
                        "December",
                        LV_ROLLER_MODE_INFINITE);

   lv_roller_set_visible_row_count(demo_roller, 3);
   lv_obj_align(demo_roller, LV_ALIGN_RIGHT_MID, -10, 0);
   lv_obj_add_event_cb(demo_roller, roller_event_cb, LV_EVENT_ALL, NULL);    

   // Swipe label
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
 * Demo page 3
 */
void demo_page3(lv_obj_t *parent)
{
   char buf[30];
   demo_butn2 = lv_btn_create(parent);
   lv_obj_set_size(demo_butn2, 110, 50);
   lv_obj_add_style(demo_butn2, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(demo_butn2, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);   
   lv_obj_set_style_radius(demo_butn2, 6, LV_PART_MAIN);   // make button a circle
   lv_obj_align(demo_butn2, LV_ALIGN_CENTER, 0, 0);   

   // Add a label the button
   drag_drop_label = lv_label_create(demo_butn2);
   lv_obj_set_size(drag_drop_label, 100, 50);   
   lv_obj_set_style_text_align(drag_drop_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_pad_top(drag_drop_label, 4, LV_PART_MAIN);
   lv_obj_center(drag_drop_label);
   lv_coord_t x = lv_obj_get_x(demo_butn2);
   lv_coord_t y = lv_obj_get_y(demo_butn2);
   sprintf(buf, "DRAG ME!\nX:%03d Y:%03d", x, y);
   lv_label_set_text(drag_drop_label, buf);   

   // ###### Add the following code to make this (or any) object draggable ######
   //
   static drag_ctx_t demo_butn2_ctx;     // drag/drop struct
   // Ensure object can receive press events; prevent layouts from snapping position
   lv_obj_add_flag(demo_butn2, LV_OBJ_FLAG_CLICKABLE);
   lv_obj_add_flag(demo_butn2, LV_OBJ_FLAG_IGNORE_LAYOUT);

   memset(&demo_butn2_ctx, 0, sizeof(drag_ctx_t)); // clear struct with 0's
   demo_butn2_ctx.drop_cb = &drop_handler_cb;      // save drag/drop function ptr
   // set event callback handler for the draggable object
   lv_obj_add_event_cb(demo_butn2, drag_abs_cb, LV_EVENT_ALL, &demo_butn2_ctx);
   demo_butn2_ctx.parent_scrollable = lv_obj_has_flag(tileview, LV_OBJ_FLAG_SCROLLABLE); // remember scroll setting
   //
   // ###### end draggable code ######

   // Swipe label
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
 * @brief Demo Page 4 - Show a QR code. 
 * @NOTE: To support QR code, do the following in lv_conf.h:
 * Set option "LV_USE_QRCODE" to 1
 */
void demo_page4(lv_obj_t *parent)
{
   // Abbycus QR code
   lv_color_t bg_color = lv_color_black(); //lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 5);
   lv_color_t fg_color = lv_palette_darken(LV_PALETTE_GREY, 1); //lv_palette_darken(LV_PALETTE_, 2);

   lv_obj_t * qr = lv_qrcode_create(parent);
   lv_qrcode_set_size(qr, 210);
   lv_qrcode_set_dark_color(qr, fg_color);
   lv_qrcode_set_light_color(qr, bg_color);

   // Set data
   const char * data = "//https://github.com/johnny49r?tab=repositories";
   lv_qrcode_update(qr, data, strlen(data));
   lv_obj_align(qr, LV_ALIGN_CENTER, 0, 0);

   // Add a border with bg_color
   lv_obj_set_style_border_color(qr, bg_color, 0);
   lv_obj_set_style_border_width(qr, 5, 0);
   
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
      Serial.println("Dropped!");
   lv_coord_t x = lv_obj_get_x(demo_butn2);
   lv_coord_t y = lv_obj_get_y(demo_butn2);
   sprintf(buf, "DRAG ME!\nX:%03d Y:%03d", x, y);
   lv_label_set_text(drag_drop_label, buf);
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click or ? event.
*/
void butn_event_cb(lv_event_t * e)
{
   uint16_t i;

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
      touch_calibrate();
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
*  @brief Start a resistive touch screen calibration by touching 4 
*  corners and saving calib data.
*/
void touch_calibrate(void)
{
#if !defined(USE_CAPACITIVE_TOUCH_SCREEN)
   uint16_t calData[5];
   uint8_t calDataOK = 0;

   // Calibrate
   tft.setRotation(SCREEN_ROTATION);
   tft.fillScreen(TFT_BLACK);
   tft.setCursor(20, 0);
   tft.setTextFont(2);
   tft.setTextSize(1);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);

   tft.println("  Touch each corner as indicated");

   tft.setTextFont(1);
   tft.println();

   tft.calibrateTouch(&calData[0], TFT_YELLOW, TFT_BLACK, 15);
   calData[4] = SCREEN_ROTATION;
   Serial.println(); Serial.println();
   Serial.println("// Use this calibration code in setup():");
   Serial.print("  uint16_t calData[5] = ");
   Serial.print("{ ");

   for (uint8_t i = 0; i < 5; i++)
   {
      Serial.print(calData[i]);
      if (i < 4) Serial.print(", ");
   }

   Serial.println(" };");
   Serial.print("  tft.setTouch(calData);");
   Serial.println(); Serial.println();

   tft.fillScreen(TFT_BLACK);
   tft.setTextColor(TFT_GREEN, TFT_BLACK);
   tft.setTextSize(2);
   tft.println("Calibration complete!");
   tft.println("Calibration values saved.");
   tft.println("Resetting...");

   vTaskDelay(3000);

   // save touch calibration data to flash memory
   if(NVS_OK) { 
      NVS.setBlob("nvs_tcal", (uint8_t *)calData, sizeof(calData), true);
   }
   ESP.restart();                         // soft reset
#endif   
}



/********************************************************************
 * Return X/Y coords from capacitive touch with FT6336 controller
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
