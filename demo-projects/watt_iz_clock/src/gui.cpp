/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// Timekeeping / NTP server(s)
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";
struct tm timeinfo;

extern "C" { LV_FONT_DECLARE(segment7_120); }
LV_IMAGE_DECLARE(clock_demo_icon_300x150_bw);
LV_IMAGE_DECLARE(icon_calendar_28_cyan);

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
#define DISPLAY_POINTS           512

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
volatile uint16_t msgBoxBtnTag = MBOX_BTN_NONE;

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
   tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
   tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
   tv_demo_page4 =  lv_tileview_add_tile(tileview, 3, 0, LV_DIR_LEFT);   

   // Associate tileview pages
   demo_page1(tv_demo_page1); 
   demo_page2(tv_demo_page2);   
   demo_page3(tv_demo_page3); 
   demo_page4(tv_demo_page4);              

   // Default to top level page
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

   // Show Project Image
   lv_obj_t * img1 = lv_image_create(parent);
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
   // clock time label
   time_label = lv_label_create(parent);
   lv_obj_set_size(time_label, 292, 130);
   lv_obj_add_style(time_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(time_label, &segment7_120, 0);
   lv_obj_set_style_text_letter_space(time_label, 0, LV_PART_MAIN); 
   lv_obj_set_style_text_color(time_label, lv_color_white(), LV_PART_MAIN);
   lv_label_set_text(time_label, "00:00");
   lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 5);   

   // clock time label
   ampm_label = lv_label_create(parent);
   lv_obj_set_size(ampm_label, 28, 70);
   lv_obj_add_style(ampm_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_bg_opa(ampm_label, LV_OPA_70, LV_PART_MAIN);
   lv_obj_set_style_bg_color(ampm_label, lv_palette_lighten(LV_PALETTE_PINK, 1), LV_PART_MAIN);   
   lv_obj_set_style_pad_bottom(ampm_label, 8, LV_PART_MAIN);
   lv_obj_set_style_pad_hor(ampm_label, 1, LV_PART_MAIN);   
   // lv_obj_set_style_text_color(ampm_label, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_color(ampm_label, lv_color_white(), LV_PART_MAIN);     
   lv_label_set_text(ampm_label, "A\nM"); 
   lv_obj_align_to(ampm_label, time_label, LV_ALIGN_OUT_RIGHT_MID, -5, -24);     

   // date label
   date_label = lv_label_create(parent);
   lv_obj_set_size(date_label, 319, 60);
   lv_obj_add_style(date_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(date_label, &lv_font_montserrat_32, LV_PART_MAIN);
   lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(date_label, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(date_label, "Sun, Jan 01, 2025");   
   lv_obj_align_to(date_label, time_label, LV_ALIGN_BOTTOM_MID, 10, 20);   

   // Calendar button
   clock_calendar_butn = lv_button_create(parent);
   lv_obj_set_size(clock_calendar_butn, 36, 36);
   lv_obj_add_style(clock_calendar_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_add_event_cb(clock_calendar_butn, butn_event_cb, LV_EVENT_CLICKED, NULL); 
   lv_obj_set_ext_click_area(clock_calendar_butn, 12);
   lv_obj_align_to(clock_calendar_butn, date_label, LV_ALIGN_OUT_BOTTOM_MID, 0, -10);   

   lv_obj_t *clock_calendar_butn_img = lv_image_create(clock_calendar_butn);
   lv_image_set_src(clock_calendar_butn_img, &icon_calendar_28_cyan);
   lv_obj_set_style_bg_opa(clock_calendar_butn_img, LV_OPA_100, LV_PART_MAIN);   
   lv_obj_center(clock_calendar_butn_img);   

   // temperature label
   temp_label = lv_label_create(parent);
   lv_obj_set_size(temp_label, 65, 36);
   lv_obj_add_style(temp_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_20, LV_PART_MAIN);
   lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_label_set_text(temp_label, "25.0C");  
   lv_obj_align(temp_label, LV_ALIGN_BOTTOM_LEFT, 5, -5);   

   // set time button
   set_time_butn = lv_button_create(parent);
   lv_obj_set_size(set_time_butn, 140, 40);
   lv_obj_add_style(set_time_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(set_time_butn, &style_butn_pressed, LV_STATE_PRESSED);  
   lv_obj_set_style_pad_hor(set_time_butn, 0, LV_PART_MAIN);     
   lv_obj_add_event_cb(set_time_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_align(set_time_butn, LV_ALIGN_BOTTOM_MID, 0, -6);   

   lv_obj_t *set_time_butn_label = lv_label_create(set_time_butn);
   lv_obj_add_style(set_time_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(set_time_butn_label, "Set DateTime");
   lv_obj_set_style_text_align(set_time_butn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);   
   lv_obj_center(set_time_butn_label);

}


/********************************************************************
 * Demo Page 3
 */
void demo_page3(lv_obj_t *parent)
{
   // create month dropdown menu
   dd_month = lv_dropdown_create(parent);
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
   lv_obj_add_event_cb(dd_month, dd_event_handler_cb, LV_EVENT_ALL, NULL);

   // set day of the month
   dd_dayOfMonth = lv_dropdown_create(parent);
   lv_obj_set_size(dd_dayOfMonth, 56, 30);
   lv_dropdown_set_options(dd_dayOfMonth, "01\n"
         "02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n"
         "17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31");                                                                                                                                                                                                                                                                                                                 

   lv_obj_align_to(dd_dayOfMonth, dd_month, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
   lv_obj_add_event_cb(dd_dayOfMonth, dd_event_handler_cb, LV_EVENT_ALL, NULL);   

   // year dropdown menu
   dd_year = lv_dropdown_create(parent);
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
   dd_hour = lv_dropdown_create(parent);
   lv_obj_set_size(dd_hour, 100, 26);

   lv_dropdown_set_options(dd_hour, "Hour=0\n"
         "Hour=1\nHour=2\nHour=3\nHour=4\nHour=5\nHour=6\nHour=7\nHour=8\nHour=9\nHour=10\n"
         "Hour=11\nHour=12\nHour=13\nHour=14\nHour=15\nHour=16\nHour=17\nHour=18\nHour=19\n"
         "Hour=20\nHour=21\nHour=22\nHour=23");   

   // lv_dropdown_set_text(dd_hour, "Hour 0");    // fixed text on dropdown                                                                                                                                                                                                                                                                                                                    
   lv_obj_align(dd_hour, LV_ALIGN_LEFT_MID, 4, -30);
   lv_obj_add_event_cb(dd_hour, dd_event_handler_cb, LV_EVENT_ALL, NULL);   

   // Minute dropdown
   dd_minute = lv_dropdown_create(parent);
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
   dd_second = lv_dropdown_create(parent);
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
   set_datetime_butn = lv_button_create(parent);
   lv_obj_set_size(set_datetime_butn, 110, 36);
   lv_obj_align(set_datetime_butn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
   lv_obj_add_style(set_datetime_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(set_datetime_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(set_datetime_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   lv_obj_t *set_datetime_butn_label = lv_label_create(set_datetime_butn);
   lv_obj_add_style(set_datetime_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(set_datetime_butn_label, "Set Time");
   lv_obj_center(set_datetime_butn_label);   

   // Cancel setting
   cancel_datetime_butn = lv_button_create(parent);
   lv_obj_set_size(cancel_datetime_butn, 110, 36);
   lv_obj_align(cancel_datetime_butn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
   lv_obj_add_style(cancel_datetime_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(cancel_datetime_butn, &style_butn_pressed, LV_STATE_PRESSED);  
   lv_obj_set_style_bg_color(cancel_datetime_butn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
   lv_obj_set_style_bg_color(cancel_datetime_butn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_PRESSED);   
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
 * Demo Page 4
 */
void demo_page4(lv_obj_t *parent)
{
   lv_obj_t *timezone_label = lv_label_create(parent);
   lv_obj_add_style(timezone_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(timezone_label, "Choose UTC TimeZone:");
    lv_obj_align(timezone_label, LV_ALIGN_TOP_MID, 0, 20); 

   // Timezone dropdown menu
   dd_timezone = lv_dropdown_create(parent);
   lv_obj_set_size(dd_timezone, 260, 30);
   lv_dropdown_set_options(dd_timezone, 
         "Howland Island [-12]\n"         // -12
         "American Samoa [-11]\n"
         "Honolulu, Hawaii [-10]\n"
         "Juneau Alaska [-9]\n"
         "San Francisco, California [-8]\n"
         "Denver, Colorado [-7]\n"
         "Dallas, Texas [-6]\n"
         "Washington, D.C. [-5]\n"
         "Caracas, Venezuela [-4]\n"
         "Rio De Janeiro, Brazil [-3]\n"
         "Greenland [-2]\n"
         "Azores [-1]\n"
         "London, England [0]\n"
         "Berlin, Germany [1]\n"
         "Cairo, Egypt [2]\n"
         "Moscow, USSR [3]\n"
         "Dubai, UAE [4]\n"
         "Islamabad, Pakistan [5]\n"
         "Bangladesh [6]\n"
         "Bangkok, Thailand [7]\n"
         "Hong Kong, China [8]\n"
         "Tokyo, Japan [9]\n"
         "Sydney, Australia [10]\n"
         "Soloman Islands [11]\n"
         "Auckland, New Zealand [12]\n"
         "Kiribati [13]\n"
         "Line Islands [14]"          // +14
      );

   lv_obj_set_ext_click_area(dd_timezone, 15);
   lv_dropdown_set_selected(dd_timezone, 12);
   lv_obj_set_style_bg_color(dd_timezone, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
   lv_obj_set_style_text_color(dd_timezone, lv_color_white(), LV_PART_MAIN);
   lv_obj_add_event_cb(dd_timezone, dd_event_handler_cb, LV_EVENT_ALL, NULL);   
   lv_obj_align_to(dd_timezone, timezone_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);   

   // set auto date/time button
   auto_datetime_butn = lv_button_create(parent);
   lv_obj_set_size(auto_datetime_butn, 180, 36);
   lv_obj_add_style(auto_datetime_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(auto_datetime_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(auto_datetime_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_set_ext_click_area(auto_datetime_butn, 12);
   lv_obj_align(auto_datetime_butn, LV_ALIGN_BOTTOM_MID, 0, -20);   

   lv_obj_t *auto_datetime_butn_label = lv_label_create(auto_datetime_butn);
   lv_obj_add_style(auto_datetime_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(auto_datetime_butn_label, "Set Internet Time");
   lv_obj_center(auto_datetime_butn_label);      
}


/********************************************************************
 * Update the display of current date & time.
 */
void refreshDateTime(const RtcDateTime& dt) 
{
   char buf[20];
   char ampm[6];
   uint8_t hour = dt.Hour();

   if(hour > 11) {
      if(hour > 12)                       // if hour == 12, it's 12 noon
         hour -= 12;
      strcpy(ampm, "P\nM");
   } else {
      if (hour == 0)
         hour = 12;                       // it must be 12 midnight
      strcpy(ampm, "A\nM");
   }
   /**
    * @brief Flash the time colon once per second. 
    * @note: The ';' is modified in the custom font file to be a blank
    * character.
    */
   if((dt.Second() % 2) == 0) {
      sprintf(buf, "%02d:%02d", hour, dt.Minute());   
   } else {
      sprintf(buf, "%02d;%02d", hour, dt.Minute());      
   }
   lv_label_set_text(time_label, buf);
   lv_label_set_text(ampm_label, ampm); 

   snprintf(buf, sizeof(buf), "%s, %s %02d, %d", 
         weekdays[dt.DayOfWeek()], months[dt.Month()-1], dt.Day(), dt.Year());
   lv_label_set_text(date_label, buf);

   // show board temp
   float tc = sys_utils.RTCGetTempC();
   sprintf(buf, "%.1fC", tc);
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
      sys_utils.RTCsetNewDateTime(yr, mon, day, hr, min, sec);
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON);      
   }
   else if(butn == clock_calendar_butn) {
      calendarPopup(false);
   }
   else if(butn == auto_datetime_butn) {
      if(WiFi.status() == WL_CONNECTED) {
         int8_t i = lv_dropdown_get_selected(dd_timezone);
         constrain(i, 0, 26);
         String tz = sys_utils.makeTzString(i - 12);
         configTzTime(tz.c_str(), NTP_SERVER_1, NTP_SERVER_2);            
         if (getLocalTime(&timeinfo)) {
            // Update RTC chip with new date/time from NTP server
            sys_utils.RTCsetNewDateTime(timeinfo.tm_year-100, timeinfo.tm_mon+1, timeinfo.tm_mday, 
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
         }            
      }
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

   /**
   *  @brief Init the Calendar style
   */
   lv_style_init(&style_calendar);   
   lv_style_set_bg_color(&style_calendar, lv_palette_darken(LV_PALETTE_GREY, 3));
   lv_style_set_bg_opa(&style_calendar, LV_OPA_COVER);
   lv_style_set_text_color(&style_calendar, lv_color_white());
   lv_style_set_border_width(&style_calendar, 2);
   lv_style_set_border_color(&style_calendar, lv_palette_main(LV_PALETTE_GREY));
   lv_style_set_radius(&style_calendar, 6);   

   /**
    *  @brief Init the HOME button default style
    */
   lv_style_init(&style_home_butn_default);
   lv_style_set_bg_opa(&style_home_butn_default, LV_OPA_TRANSP);
   lv_style_set_radius(&style_home_butn_default, 8);
   lv_style_set_bg_color(&style_home_butn_default, lv_color_black());
   lv_style_set_border_width(&style_home_butn_default, 2);
   lv_style_set_border_color(&style_home_butn_default, lv_palette_lighten(LV_PALETTE_CYAN, 1));
   lv_style_set_border_opa(&style_home_butn_default, LV_OPA_COVER);
   lv_style_set_shadow_width(&style_home_butn_default, 0);
   lv_style_set_outline_width(&style_home_butn_default, 0);       // no outline 
   lv_style_set_pad_all(&style_home_butn_default, 0);   
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

/********************************************************************
 * @brief Translator Results Popup
 */
static void calendarPopup(bool use_chinese)
{
   // --- Calendar popup container --- 
   calendar_popup_cont = lv_obj_create(lv_scr_act());
   lv_obj_set_size(calendar_popup_cont, 320, 240);
   lv_obj_center(calendar_popup_cont);
   lv_obj_set_scrollbar_mode(calendar_popup_cont, LV_SCROLLBAR_MODE_OFF);     // no scrolling!
   lv_obj_clear_flag(calendar_popup_cont, LV_OBJ_FLAG_SCROLLABLE);
   lv_obj_set_style_bg_color(calendar_popup_cont, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(calendar_popup_cont, LV_OPA_COVER, LV_PART_MAIN);
   lv_obj_set_style_pad_all(calendar_popup_cont, 2, LV_PART_MAIN);
   lv_obj_set_style_radius(calendar_popup_cont, 6, LV_PART_MAIN);
   lv_obj_set_style_border_width(calendar_popup_cont, 0, LV_PART_MAIN);

   // Settings page title
   lv_obj_t *calendar_title_label = lv_label_create(calendar_popup_cont);
   lv_obj_set_size(calendar_title_label, 150, 34);
   lv_obj_add_style(calendar_title_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(calendar_title_label, 0, LV_PART_MAIN);
   lv_label_set_text(calendar_title_label, "CALENDAR");
   lv_obj_set_style_text_color(calendar_title_label, lv_palette_lighten(LV_PALETTE_DEEP_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(calendar_title_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(calendar_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(calendar_title_label, LV_ALIGN_TOP_MID, 0, 0); 

   // Translate home button
   calendar_close_butn = lv_button_create(calendar_popup_cont);
   lv_obj_set_size(calendar_close_butn, 34, 34);
   lv_obj_add_style(calendar_close_butn, &style_home_butn_default, LV_PART_MAIN);  
   lv_obj_add_event_cb(calendar_close_butn, calendar_btn_event_cb, LV_EVENT_CLICKED, NULL); 
   lv_obj_set_ext_click_area(calendar_close_butn, 10);   
   lv_obj_align(calendar_close_butn, LV_ALIGN_TOP_RIGHT, 0, 0);    
   lv_obj_t *calendar_close_butn_lbl = lv_label_create(calendar_close_butn);
   lv_obj_set_style_text_font(calendar_close_butn_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
   lv_obj_set_style_text_align(calendar_close_butn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(calendar_close_butn_lbl, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(calendar_close_butn_lbl, LV_SYMBOL_CLOSE);    
   lv_obj_center(calendar_close_butn_lbl);    

   lv_obj_t *calendar = lv_calendar_create(calendar_popup_cont);
   lv_obj_set_size(calendar, 310, 194);
   lv_obj_add_style(calendar, &style_calendar, LV_PART_MAIN);
   lv_calendar_add_header_arrow(calendar);   // allow scrolling to next/prev months

   // Get internal grid (buttonmatrix) and style it 
   lv_obj_t *btnm = lv_calendar_get_btnmatrix(calendar);
   lv_obj_set_style_text_color(btnm, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
   lv_obj_set_style_bg_color(btnm, lv_color_hex(0x000000), LV_PART_ITEMS);
   lv_obj_add_event_cb(calendar, calendar_event_handler, LV_EVENT_ALL, NULL);
   lv_obj_align(calendar, LV_ALIGN_BOTTOM_MID, 0, 0);   

   RtcDateTime cur_time = sys_utils.RTCgetDateTime(); // get cur date & time         
   lv_calendar_set_today_date(calendar, cur_time.Year(), cur_time.Month(), cur_time.Day());
   lv_calendar_set_month_shown(calendar, cur_time.Year(), cur_time.Month());
}

/**
 * @brief Event handler for transient 
 */
static void calendar_btn_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);
   if(butn == calendar_close_butn) {
      lv_obj_del(calendar_popup_cont);
   }
}


/********************************************************************
 * @brief Handle calendar press events
 */
void calendar_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_current_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_calendar_date_t date;
        if(lv_calendar_get_pressed_date(obj, &date)) {
            Serial.printf("Clicked date: %02d.%02d.%d", date.day, date.month, date.year);
        }
    }
}