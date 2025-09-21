/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
#define DISPLAY_POINTS           512

// Graphics hardware library
TFT_eSPI tft = TFT_eSPI();
#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   uint16_t calData[5] = { 360, 3400, 360, 3400, 7 }; // Example — use your own from calibration
#endif

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib

// Date Time
const char weekdays[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
RtcDateTime local_dt;   

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
   tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

   // build tile pages
   demo_page1(tv_demo_page1);             // build the top level switch page
   demo_page2(tv_demo_page2);             // build the top level switch page   
   demo_page3(tv_demo_page3);             // build the top level switch page 

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_OFF);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   LV_IMAGE_DECLARE(clock_demo_icon_300x150_bw);
   
   scrn1 = lv_obj_create(parent);          // create a container for the widgets
   lv_obj_set_style_bg_color(scrn1, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1, 0, LV_PART_MAIN);
   lv_obj_set_style_pad_all(scrn1, 0, LV_PART_MAIN);
   lv_obj_set_size(scrn1, SCREEN_WIDTH, SCREEN_HEIGHT);
   lv_obj_center(scrn1);   

   // show demo info
   lv_obj_t *scrn1_demo_label = lv_label_create(scrn1);
   lv_obj_set_size(scrn1_demo_label, 320, 40);
   lv_obj_add_style(scrn1_demo_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1_demo_label, 0, LV_PART_MAIN);
   lv_label_set_text(scrn1_demo_label, "WATT-IZ DEMO PROJECTS");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 5);      

   // project image
   lv_obj_t * img1 = lv_image_create(scrn1);
   lv_image_set_src(img1, &clock_demo_icon_300x150_bw);
   lv_obj_align_to(img1, scrn1_demo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0); 

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
   scrn2 = lv_obj_create(parent);          // create a container for the widgets
   lv_obj_set_style_bg_color(scrn2, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn2, 0, LV_PART_MAIN);
   lv_obj_set_style_pad_all(scrn2, 0, LV_PART_MAIN);
   lv_obj_set_size(scrn2, SCREEN_WIDTH, SCREEN_HEIGHT);
   lv_obj_center(scrn2);   

   // clock time label
   time_label = lv_label_create(scrn2);
   lv_obj_set_size(time_label, 230, 60);
   lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 10, 5);
   lv_obj_add_style(time_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, LV_PART_MAIN);
   lv_label_set_text(time_label, "12:00:00");

   // clock time label
   ampm_label = lv_label_create(scrn2);
   lv_obj_set_size(ampm_label, 60, 40);
   lv_obj_align_to(ampm_label, time_label, LV_ALIGN_OUT_RIGHT_MID, 2, -2);
   lv_obj_add_style(ampm_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_bg_opa(ampm_label, LV_OPA_70, LV_PART_MAIN);
   lv_obj_set_style_bg_color(ampm_label, lv_palette_lighten(LV_PALETTE_PINK, 1), LV_PART_MAIN);   
   // lv_obj_set_style_text_color(ampm_label, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_28, LV_PART_MAIN);
   lv_label_set_text(ampm_label, "AM");   

   // date label
   date_label = lv_label_create(scrn2);
   lv_obj_set_size(date_label, 300, 60);
   lv_obj_align(date_label, LV_ALIGN_LEFT_MID, -12, -32);
   lv_obj_add_style(date_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(date_label, &lv_font_montserrat_28, LV_PART_MAIN);
   lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_label_set_text(date_label, "Sun, Jan 01, 2025");   

   // temperature label
   temp_label = lv_label_create(scrn2);
   lv_obj_set_size(temp_label, 300, 60);
   lv_obj_align(temp_label, LV_ALIGN_LEFT_MID, -40, 36);
   lv_obj_add_style(temp_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_18, LV_PART_MAIN);
   lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_label_set_text(temp_label, "Board Temp = 25.0C");  

   // set time button
   set_time_butn = lv_button_create(scrn2);
   lv_obj_set_size(set_time_butn, 150, 40);
   lv_obj_align(set_time_butn, LV_ALIGN_BOTTOM_MID, 10, -10);
   lv_obj_add_style(set_time_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(set_time_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(set_time_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   lv_obj_t *set_time_butn_label = lv_label_create(set_time_butn);
   lv_obj_add_style(set_time_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(set_time_butn_label, "Set Date/Time");
   lv_obj_center(set_time_butn_label);

}


/********************************************************************
 * Demo Page 3
 */
void demo_page3(lv_obj_t *parent)
{
   scrn3 = lv_obj_create(parent);          // create a container for the widgets
   lv_obj_set_style_bg_color(scrn3, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn3, 0, LV_PART_MAIN);
   lv_obj_set_style_pad_all(scrn3, 0, LV_PART_MAIN);
   lv_obj_set_size(scrn3, SCREEN_WIDTH, SCREEN_HEIGHT);
   lv_obj_center(scrn3);    

   // set month
   dd_month = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_month, 80, 30);
   lv_dropdown_set_options(dd_month, "Jan\n"
                           "Feb\n"
                           "Mar\n"
                           "Apr\n"
                           "May\n"
                           "Jun\n"
                           "Jul\n"
                           "Aug\n"
                           "Sep\n"  
                           "Oct\n"
                           "Nov\n"
                           "Dec");

   lv_obj_align(dd_month, LV_ALIGN_TOP_LEFT, 0, 20);
   // lv_obj_align_to(dd_month, dd_dayOfWeek, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_month, dd_event_handler_cb, LV_EVENT_ALL, NULL);

   // set day of the month
   dd_dayOfMonth = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_dayOfMonth, 56, 30);
   lv_dropdown_set_options(dd_dayOfMonth, "01\n"
         "02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n"
         "17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31");                                                                                                                                                                                                                                                                                                                 

   lv_obj_align_to(dd_dayOfMonth, dd_month, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_dayOfMonth, dd_event_handler_cb, LV_EVENT_ALL, NULL);   

   // year dropdown menu
   dd_year = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_year, 80, 30);
   lv_dropdown_set_options(dd_year, "2025\n"
         "2026\n"
         "2027\n"
         "2028\n"
         "2029\n"
         "2030\n"
         "2031\n"
         "2032\n"
         "2033\n"  
         "2034\n"
         "2035");

   lv_obj_align_to(dd_year, dd_dayOfMonth, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_year, dd_event_handler_cb, LV_EVENT_ALL, NULL);   

   // Hour dropdown
   dd_hour = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_hour, 100, 26);

   lv_dropdown_set_options(dd_hour, "Hour=0\n"
         "Hour=1\nHour=2\nHour=3\nHour=4\nHour=5\nHour=6\nHour=7\nHour=8\nHour=9\nHour=10\n"
         "Hour=11\nHour=12\nHour=13\nHour=14\nHour=15\nHour=16\nHour=17\nHour=18\nHour=19\n"
         "Hour=20\nHour=21\nHour=22\nHour=23");   

   // lv_dropdown_set_text(dd_hour, "Hour 0");    // fixed text on dropdown                                                                                                                                                                                                                                                                                                                    
   lv_obj_align(dd_hour, LV_ALIGN_LEFT_MID, 4, -30);
   lv_obj_add_event_cb(dd_hour, dd_event_handler_cb, LV_EVENT_ALL, NULL);   

   // Minute dropdown
   dd_minute = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_minute, 100, 26);

   lv_dropdown_set_options(dd_minute, "Min=0\n"
         "Min=1\nMin=2\nMin=3\nMin=4\nMin=5\nMin=6\nMin=7\nMin=8\nMin=9\nMin=10\n"
         "Min=11\nMin=12\nMin=13\nMin=14\nMin=15\nMin=16\nMin=17\nMin=18\nMin=19\n"
         "Min=20\nMin=21\nMin=22\nMin=23\nMin=24\nMin=25\nMin=26\nMin=27\nMin=28\n"
         "Min=29\nMin=30\nMin=31\nMin=32\nMin=33\nMin=34\nMin=35\nMin=36\nMin=37\n"
         "Min=38\nMin=39\nMin=40\nMin=41\nMin=42\nMin=43\nMin=44\nMin=45\nMin=46\n"
         "Min=47\nMin=48\nMin=49\nMin=50\nMin=51\nMin=52\nMin=53\nMin=54\nMin=55\n"  
         "Min=56\nMin=57\nMin=58\nMin=59");   
                                                                                                                                                                                                                                                                                                             
   lv_obj_align_to(dd_minute, dd_hour, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_minute, dd_event_handler_cb, LV_EVENT_ALL, NULL);      

   // Second dropdown
   dd_second = lv_dropdown_create(scrn3);
   lv_obj_set_size(dd_second, 100, 26);

   lv_dropdown_set_options(dd_second, "Sec=0\n"
         "Sec=1\nSec=2\nSec=3\nSec=4\nSec=5\nSec=6\nSec=7\nSec=8\nSec=9\nSec=10\n"
         "Sec=11\nSec=12\nSec=13\nSec=14\nSec=15\nSec=16\nSec=17\nSec=18\nSec=19\n"
         "Sec=20\nSec=21\nSec=22\nSec=23\nSec=24\nSec=25\nSec=26\nSec=27\nSec=28\n"
         "Sec=29\nSec=30\nSec=31\nSec=32\nSec=33\nSec=34\nSec=35\nSec=36\nSec=37\n"
         "Sec=38\nSec=39\nSec=40\nSec=41\nSec=42\nSec=43\nSec=44\nSec=45\nSec=46\n"
         "Sec=47\nSec=48\nSec=49\nSec=50\nSec=51\nSec=52\nSec=53\nSec=54\nSec=55\n"  
         "Sec=56\nSec=57\nSec=58\nSec=59");   
                                                                                                                                                                                                                                                                                                             
   lv_obj_align_to(dd_second, dd_minute, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_second, dd_event_handler_cb, LV_EVENT_ALL, NULL);      

   // set date/time button
   set_datetime_butn = lv_button_create(scrn3);
   lv_obj_set_size(set_datetime_butn, 100, 36);
   lv_obj_align(set_datetime_butn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
   lv_obj_add_style(set_datetime_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(set_datetime_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(set_datetime_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   lv_obj_t *set_datetime_butn_label = lv_label_create(set_datetime_butn);
   lv_obj_add_style(set_datetime_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(set_datetime_butn_label, "Set New");
   lv_obj_center(set_datetime_butn_label);   

   // Cancel setting
   cancel_datetime_butn = lv_button_create(scrn3);
   lv_obj_set_size(cancel_datetime_butn, 100, 36);
   lv_obj_align(cancel_datetime_butn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
   lv_obj_add_style(cancel_datetime_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(cancel_datetime_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(cancel_datetime_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   lv_obj_t *cancel_datetime_butn_label = lv_label_create(cancel_datetime_butn);
   lv_obj_add_style(cancel_datetime_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(cancel_datetime_butn_label, "Cancel");
   lv_obj_center(cancel_datetime_butn_label);     
}


/********************************************************************
*  @fn Dropdown menu event handler. 
*/
void dd_event_handler_cb(lv_event_t * e)
{
   char buf[20];
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);

   // if(dd == dd_hour && code == LV_EVENT_VALUE_CHANGED) {
   //    uint16_t hr = lv_dropdown_get_selected(dd_hour);
   //    sprintf(buf, "Hour %d", hr);     
   //       // Serial.println(buf); 
   //    lv_dropdown_set_text(dd_hour, buf);  
   // }   
}


/********************************************************************
 * Update the display of current date & time.
 */
void refreshDateTime(const RtcDateTime& dt) 
{
   char buf[20];
   char ampm[3];
   uint8_t hour = dt.Hour();

   if(hour > 11) {
      if(hour > 12)                       // if hour == 12, it's 12 noon
         hour -= 12;
      strcpy(ampm, "PM");
   } else {
      if (hour == 0)
         hour = 12;                       // it must be 12 midnight
      strcpy(ampm, "AM");
   }
   sprintf(buf, "%02d:%02d:%02d", hour, dt.Minute(), dt.Second());
   lv_label_set_text(time_label, buf);
   lv_label_set_text(ampm_label, ampm); 

   snprintf(buf, sizeof(buf), "%s, %s %02d, %d", 
         weekdays[dt.DayOfWeek()], months[dt.Month()-1], dt.Day(), dt.Year());
   lv_label_set_text(date_label, buf);

   // show board temp
   float tc = sys_utils.RTCGetTempC();
   sprintf(buf, "Board Temp = %.1fC", tc);
   lv_label_set_text(temp_label, buf); 
}



/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   char buf[32];
   String ss;
   uint16_t yr;
   uint8_t mon, day, hr, min, sec;
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(butn == next_butn) {
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON); 
   }
   else if(butn == set_time_butn && code == LV_EVENT_CLICKED) {
      Serial.println("butn clicked");
      lv_obj_set_tile(tileview, tv_demo_page3, LV_ANIM_ON);   // default starting page
   }   
   else if(butn == cancel_datetime_butn && code == LV_EVENT_CLICKED) {
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON);
   }
   else if(butn == set_datetime_butn && code == LV_EVENT_CLICKED) {
      yr = 2025 + lv_dropdown_get_selected(dd_year);  // 0 = 2025
      mon = lv_dropdown_get_selected(dd_month) + 1;   // month is 1-12
      day = lv_dropdown_get_selected(dd_dayOfMonth) + 1; 
      hr = lv_dropdown_get_selected(dd_hour);
      min = lv_dropdown_get_selected(dd_minute);   
      sec = lv_dropdown_get_selected(dd_second);          
               Serial.printf("yr=%d, mon=%d, day=%d\n", yr, mon, day);
      sys_utils.RTCsetNewDateTime(yr, mon, day, hr, min, sec);
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON);      
   }
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

