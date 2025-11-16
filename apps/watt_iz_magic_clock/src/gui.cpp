/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

/**
 * @brief Declare fonts and icon images 
 */
extern "C" { LV_FONT_DECLARE(segment7_120); }
LV_IMAGE_DECLARE(icon_translate_48_cyan);
LV_IMAGE_DECLARE(icon_digital_clock_cyan);
LV_IMAGE_DECLARE(icon_chatbot_48_cyan);
LV_IMAGE_DECLARE(icon_intercom_48_cyan);
LV_IMAGE_DECLARE(icon_settings_48_cyan);
LV_IMAGE_DECLARE(icon_recorder_48_cyan);
LV_IMAGE_DECLARE(icon_alarms_48_cyan);

// Supported fonts
// For other noto sans fonts, goto https://fonts.google.com/
LV_FONT_DECLARE(noto_thai_18);
LV_FONT_DECLARE(noto_latin_18);
LV_FONT_DECLARE(noto_pt_18);              // Portuguese
LV_FONT_DECLARE(noto_sans_sc_18);
LV_FONT_DECLARE(noto_ru_18);
LV_FONT_DECLARE(noto_JP_18);
LV_FONT_DECLARE(noto_hindi_18);

// NOTE: Chinese & Japanese have huge numbers of symbols in their written language. The
// fonts created here for the ESP32-S3 with limited resources may produce 'tofu' symbols
// instead of the desired symbol. Maybe larger fonts can be created and used by the
// lvgl lv_load_font() function which keeps fonts as a file on the SD card instead of
// being compiled and saved in flash. TBD...

#define DEFAULT_SPEAKING_RATE    0.80     // 0.0 - 1.0 rate of speaking
#define NUM_LANGUAGES            13

/**
 * @brief Translator Language Array. Each element contains:
 * 1) Name of the selected language (Ex: "English")
 * 2) The google speech code (Ex: "en-US")
 * 3) Google voice name (Ex: "en-US-Wavenet-C")
 * 4) Pointer to custom font (Ex: &noto_latin_18)
 * 5) Speaking rate. Float value from 0.0 to 1.0 (mas speak rate)
 * For info on google language codes and voice names see:
 *    https://cloud.google.com/text-to-speech/docs/list-voices-and-types
 */
language_info_t LanguageArray[NUM_LANGUAGES] = {
   {
      "Chinese", "zh-CN", "cmn-CN-Wavenet-A", &noto_sans_sc_18, DEFAULT_SPEAKING_RATE
   },
   {
      "English", "en-US", "en-US-Wavenet-C", &noto_latin_18, DEFAULT_SPEAKING_RATE
   },
   {
      "French", "fr-FR", "fr-FR-Wavenet-F", &noto_latin_18, DEFAULT_SPEAKING_RATE
   },
   {
      "German", "de-DE", "de-DE-Wavenet-G", &noto_latin_18, DEFAULT_SPEAKING_RATE       
   }, 
   {
      "Hindi(India)", "hi-IN", "hi-IN-Wavenet-A", &noto_hindi_18, DEFAULT_SPEAKING_RATE       
   },       
   {
      "Italian", "it-IT", "it-IT-Wavenet-E", &noto_latin_18, DEFAULT_SPEAKING_RATE       
   },
   {
      "Japanese", "ja-JP", "ja-JP-Wavenet-A", &noto_JP_18, DEFAULT_SPEAKING_RATE       
   },  
   {
      "Norwegian", "nb-NO", "nb-NO-Wavenet-F", &noto_ru_18, DEFAULT_SPEAKING_RATE        
   },      
   {
      "Portuguese", "pt-PT", "pt-PT-Wavenet-E", &noto_pt_18, DEFAULT_SPEAKING_RATE        
   },           
   {
      "Russian", "ru-RU", "ru-RU-Wavenet-A", &noto_ru_18, DEFAULT_SPEAKING_RATE        
   },        
   {
      "Spanish", "es-ES", "es-ES-Wavenet-F", &noto_latin_18, DEFAULT_SPEAKING_RATE        
   },       
   {
      "Swedish", "sv-SE", "sv-SE-Wavenet-F", &noto_latin_18, DEFAULT_SPEAKING_RATE        
   },          
   {
      "Thai", "th-TH", "th-TH-Standard-A", &noto_thai_18, DEFAULT_SPEAKING_RATE    
   }                    
};


// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
int16_t gui_event;
uint8_t updateReplayLabel = REPLAY_UPDATE_NONE;
#define DISPLAY_POINTS           512

// Date / Time
int8_t last_sec = -1;
const char weekdays[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
RtcDateTime now;

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
   // Initialize LCD GPIO's
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off   
   vTaskDelay(100);
 
   // Configure TFT backlight dimming PWM
   ledcSetup(BKLT_CHANNEL, BKLT_FREQ, BKLT_RESOLUTION);
   ledcAttachPin(PIN_LCD_BKLT, BKLT_CHANNEL);  // attach the channel to GPIO pin to control dimming      
   setBacklight(255);                     // full brightness

   // GUI events
   gui_event = GUI_EVENT_NONE;

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
void pageBuilder(void)
{
   tileview = lv_tileview_create(lv_screen_active());
   lv_obj_add_style(tileview, &style_tileview, LV_PART_MAIN);
   lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
   lv_obj_add_event_cb(tileview, tileview_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

   // Create tiles
   tv_page1 =  lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
   tv_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
   tv_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
   tv_page4 =  lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
   tv_page5 =  lv_tileview_add_tile(tileview, 4, 0, LV_DIR_HOR);   
   tv_page6 =  lv_tileview_add_tile(tileview, 5, 0, LV_DIR_HOR);
   tv_page7 =  lv_tileview_add_tile(tileview, 6, 0, LV_DIR_HOR);
   tv_page8 =  lv_tileview_add_tile(tileview, 7, 0, LV_DIR_LEFT);     

   // build tile pages
   gui_page1(tv_page1);            
   gui_page2(tv_page2);        
   gui_page3(tv_page3); 
   gui_page4(tv_page4); 
   gui_page5(tv_page5); 
   gui_page6(tv_page6);
   gui_page7(tv_page7);
   gui_page8(tv_page8);

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_timer_create(inactivity_timer_cb, 2000, NULL);  // check for page inactivity 
   lv_obj_set_tile(tileview, tv_page1, LV_ANIM_OFF);  // default starting page
}


/********************************************************************
 * @brief Service tile page inactivity.
 */
void inactivity_timer_cb(lv_timer_t *t)
{
   lv_display_t *d = lv_display_get_default();
   uint32_t inactive_ms = lv_display_get_inactive_time(d);

   // React to a page that has been inactive for some period of time
      // if(lv_tileview_get_tile_act(tileview) == tv_page3) {  // only active on this page
      //    if(inactive_ms >= 60000) {   // 1 minute
      //       lv_obj_set_tile(tileview, tv_page1, LV_ANIM_ON);
      //    }
      // }
}


/********************************************************************
 * Magic clock Page 1
 */
void gui_page1(lv_obj_t *parent)
{
   uint16_t cx = 158;
   uint16_t cy = 115;
   uint16_t radius_px = 88;
   // Create page 1 selector wheel
   float angle_start_deg = -90.0f;
   float step_deg        = 360.0f / ICON_COUNT;

   for (int i = 0; i < ICON_COUNT; i++) {
      float ang_deg = angle_start_deg + step_deg * i;
      float ang_rad = ang_deg * 3.1415926f / 180.0f;

      int x = cx + (int)(cosf(ang_rad) * radius_px) - (ICON_W / 2);
      int y = cy + (int)(sinf(ang_rad) * radius_px) - (ICON_H / 2);

      // Create image button and add style for pressed state
      lv_obj_t *img = lv_imagebutton_create(parent);
      lv_obj_add_style(img, &icon_style_pressed, LV_STATE_PRESSED);

      // select icon image source
      switch(i) {
         case ICON_CLOCK:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED, 
                  &icon_digital_clock_cyan, &icon_digital_clock_cyan, &icon_digital_clock_cyan);
            break;

         case ICON_ALARM:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,          
                  &icon_alarms_48_cyan, &icon_alarms_48_cyan, &icon_alarms_48_cyan);
            break;  

         case ICON_CHATBOT:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,           
                  &icon_chatbot_48_cyan, &icon_chatbot_48_cyan, &icon_chatbot_48_cyan);         
            break;      

         case ICON_TRANSLATE:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,          
                  &icon_translate_48_cyan, &icon_translate_48_cyan, &icon_translate_48_cyan);
            break;
            
         case ICON_AUDIO:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,          
                  &icon_recorder_48_cyan, &icon_recorder_48_cyan, &icon_recorder_48_cyan);
            break;            

         case ICON_INTERCOM:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,          
                  &icon_intercom_48_cyan, &icon_intercom_48_cyan, &icon_intercom_48_cyan);
            break;
            
         case ICON_SETTINGS:
            lv_imagebutton_set_src(img, LV_IMAGEBUTTON_STATE_RELEASED,          
                  &icon_settings_48_cyan, &icon_settings_48_cyan, &icon_settings_48_cyan);   
            break;
      }

      lv_obj_set_pos(img, x, y);          // set icon's rotary position 
      lv_obj_set_size(img, ICON_W, ICON_H);

      lv_obj_set_user_data(img, (void *)(uintptr_t)i);   // assign id to this image butn
      lv_obj_add_event_cb(img, launcher_btn_event_cb, LV_EVENT_CLICKED, NULL);

      // icon_obj[i] = img;

      // Image button label
      lv_obj_t *lbl = lv_label_create(parent);
      lv_label_set_text(lbl, icon_label_text((icon_id_t)i));
      lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
      lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_align_to(lbl, img, LV_ALIGN_OUT_BOTTOM_MID, 0, -2);
   }
}


/********************************************************************
 * @brief Return text associated with icon label
 */
const char *icon_label_text(icon_id_t id)
{
   switch(id) {
      case ICON_CLOCK:        return "Clock";
      case ICON_ALARM:        return "Alarm";                  
      case ICON_INTERCOM:     return "Intercom";  
      case ICON_TRANSLATE:    return "Translate";             
      case ICON_CHATBOT:      return "ChatBot";
      case ICON_AUDIO:        return "Audio";       
      case ICON_SETTINGS:     return "Settings";        
      default:                return "?";
   }
}


/********************************************************************
 * GUI Page 2 - Digital clock
 */
void gui_page2(lv_obj_t *parent)
{
   // clock time label
   time_label = lv_label_create(parent);
   lv_obj_set_size(time_label, 292, 130);
   lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 5);
   lv_obj_add_style(time_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(time_label, &segment7_120, 0);
   lv_obj_set_style_text_letter_space(time_label, 0, LV_PART_MAIN); 
   lv_obj_set_style_text_color(time_label, lv_color_white(), LV_PART_MAIN);
   lv_label_set_text(time_label, "00:00");

   // am/pm label
   ampm_label = lv_label_create(parent);
   lv_obj_set_size(ampm_label, 28, 70);
   lv_obj_align_to(ampm_label, time_label, LV_ALIGN_OUT_RIGHT_MID, -5, -24);
   lv_obj_add_style(ampm_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_bg_opa(ampm_label, LV_OPA_70, LV_PART_MAIN);
   lv_obj_set_style_bg_color(ampm_label, lv_palette_lighten(LV_PALETTE_PINK, 1), LV_PART_MAIN);   
   lv_obj_set_style_pad_bottom(ampm_label, 8, LV_PART_MAIN);
   lv_obj_set_style_pad_hor(ampm_label, 1, LV_PART_MAIN);   
   // lv_obj_set_style_text_color(ampm_label, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_color(ampm_label, lv_color_white(), LV_PART_MAIN);     
   lv_label_set_text(ampm_label, "A\nM");   

   // date label
   date_label = lv_label_create(parent);
   lv_obj_set_size(date_label, 319, 60);
   lv_obj_align_to(date_label, time_label, LV_ALIGN_BOTTOM_MID, 10, 28);
   lv_obj_add_style(date_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(date_label, &lv_font_montserrat_32, LV_PART_MAIN);
   lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(date_label, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(date_label, "Sun, Jan 01, 2025");   

   // home button
   clock_home_butn = lv_button_create(parent);
   lv_obj_set_size(clock_home_butn, 44, 44);
   lv_obj_align(clock_home_butn, LV_ALIGN_BOTTOM_MID, 0, -15);
   lv_obj_add_style(clock_home_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_add_event_cb(clock_home_butn, butn_event_cb, LV_EVENT_ALL, NULL); 

   lv_obj_t *clock_home_butn_lbl = lv_label_create(clock_home_butn);
   lv_obj_center(clock_home_butn_lbl);
   lv_obj_set_style_text_font(clock_home_butn_lbl, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_align(clock_home_butn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(clock_home_butn_lbl, lv_palette_lighten(LV_PALETTE_CYAN, 1), LV_PART_MAIN);   
   lv_label_set_text(clock_home_butn_lbl, LV_SYMBOL_HOME);   

   // WiFi label
   clock_wifi_label = lv_label_create(parent);
   lv_obj_add_style(clock_wifi_label, &style_wifi_label, LV_PART_MAIN);   
   lv_label_set_text(clock_wifi_label, LV_SYMBOL_WIFI); 
   lv_obj_align(clock_wifi_label, LV_ALIGN_BOTTOM_LEFT, 10, -15);

   // Battery SOC label
   clock_battery_label = lv_label_create(parent);
   lv_obj_add_style(clock_battery_label, &style_battery_label, LV_PART_MAIN);     
   lv_label_set_text(clock_battery_label, LV_SYMBOL_BATTERY_3); 
   lv_obj_align(clock_battery_label, LV_ALIGN_BOTTOM_RIGHT, -10, -15);   
   
   /**
    * @brief Timer to update clock and wifi / battery status
    */
   clock_timer = lv_timer_create([](lv_timer_t *timer) {   
      static uint32_t time_sync = millis();
      if(lv_tileview_get_tile_act(tileview) == tv_page2) {  // only active on this page
         // Update clock if needed
         refreshDateTime();

         // Update WiFi connection
         updateWifiLabel(clock_wifi_label);

         // Update battery SOC
         updateBatteryLabel(clock_battery_label);        
      }
   }, 200, NULL); 
}


/********************************************************************
 * GUI Page 3 - Alarm page
 */
void gui_page3(lv_obj_t *parent)
{
   // Show Alarm title
   lv_obj_t *alarm_title_label = lv_label_create(parent);
   lv_obj_set_size(alarm_title_label, 320, 36);
   lv_obj_add_style(alarm_title_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(alarm_title_label, 0, LV_PART_MAIN);
   lv_label_set_text(alarm_title_label, "ALARM SETTINGS");
   lv_obj_set_style_text_color(alarm_title_label, lv_palette_lighten(LV_PALETTE_DEEP_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(alarm_title_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(alarm_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(alarm_title_label, LV_ALIGN_TOP_MID, 0, 5); 

   // Home button
   alarm_home_butn = lv_button_create(parent);
   lv_obj_set_size(alarm_home_butn, 44, 44);
   lv_obj_align(alarm_home_butn, LV_ALIGN_BOTTOM_MID, 0, -15);
   lv_obj_add_style(alarm_home_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_add_event_cb(alarm_home_butn, butn_event_cb, LV_EVENT_ALL, NULL); 

   lv_obj_t *alarm_home_butn_lbl = lv_label_create(alarm_home_butn);
   lv_obj_center(alarm_home_butn_lbl);
   lv_obj_set_style_text_font(alarm_home_butn_lbl, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_align(alarm_home_butn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(alarm_home_butn_lbl, lv_palette_lighten(LV_PALETTE_CYAN, 1), LV_PART_MAIN);   
   lv_label_set_text(alarm_home_butn_lbl, LV_SYMBOL_HOME);   
}


/********************************************************************
 * GUI Page 4 - ChatBot page
 */
void gui_page4(lv_obj_t *parent)
{
   // ChatBot title
   lv_obj_t *chat_title_label = lv_label_create(parent);
   lv_obj_set_size(chat_title_label, 160, 36);
   lv_obj_add_style(chat_title_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(chat_title_label, 0, LV_PART_MAIN);
   lv_label_set_text(chat_title_label, "CHATBOT");
   lv_obj_set_style_text_color(chat_title_label, lv_palette_lighten(LV_PALETTE_DEEP_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(chat_title_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(chat_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(chat_title_label, LV_ALIGN_TOP_MID, 0, 5); 

   // WiFi label
   chat_wifi_label = lv_label_create(parent);
   lv_obj_add_style(chat_wifi_label, &style_wifi_label, LV_PART_MAIN);   
   lv_label_set_text(chat_wifi_label, LV_SYMBOL_WIFI); 
   lv_obj_align(chat_wifi_label, LV_ALIGN_TOP_LEFT, 10, 10);

   // Battery SOC label
   chat_battery_label = lv_label_create(parent);
   lv_obj_add_style(chat_battery_label, &style_battery_label, LV_PART_MAIN);     
   lv_label_set_text(chat_battery_label, LV_SYMBOL_BATTERY_3); 
   lv_obj_align(chat_battery_label, LV_ALIGN_TOP_RIGHT, -10, 10);        

   // Start chat button. 
   start_chat_butn = lv_imagebutton_create(parent);
   lv_obj_set_size(start_chat_butn, 48, 48);   
   lv_obj_add_style(start_chat_butn, &icon_style_pressed, LV_STATE_PRESSED);
   lv_imagebutton_set_src(start_chat_butn, LV_IMAGEBUTTON_STATE_RELEASED,           
               &icon_chatbot_48_cyan, &icon_chatbot_48_cyan, &icon_chatbot_48_cyan); 
   lv_obj_align(start_chat_butn, LV_ALIGN_CENTER, 0, -30);
   lv_obj_add_event_cb(start_chat_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   start_chat_butn_label = lv_label_create(parent);
   lv_obj_set_size(start_chat_butn_label, 100, 36);   
   lv_obj_add_style(start_chat_butn_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_align(start_chat_butn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_label_set_text(start_chat_butn_label, "Start");
   lv_obj_align_to(start_chat_butn_label, start_chat_butn, LV_ALIGN_OUT_BOTTOM_MID, -3, 5);

   // Create a progress Arc
   chat_progress_arc = lv_arc_create(parent);
   lv_obj_set_size(chat_progress_arc, 76, 76);
   lv_arc_set_rotation(chat_progress_arc, 135);
   lv_arc_set_bg_angles(chat_progress_arc, 0, 270);
   lv_arc_set_range(chat_progress_arc, 0, 100);
   lv_arc_set_value(chat_progress_arc, 0);
   lv_obj_remove_style(chat_progress_arc, NULL, LV_PART_KNOB);
   lv_obj_remove_flag(chat_progress_arc, LV_OBJ_FLAG_CLICKABLE);  // no touch action
   // Set the arc thickness (line width in pixels) and color scheme
   lv_obj_set_style_arc_width(chat_progress_arc, 5, LV_PART_INDICATOR);  // indicator arc
   lv_obj_set_style_arc_width(chat_progress_arc, 5, LV_PART_MAIN);       // background arc
   lv_obj_set_style_arc_color(chat_progress_arc, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
   lv_obj_set_style_arc_color(chat_progress_arc, lv_palette_lighten(LV_PALETTE_CYAN, 2), LV_PART_INDICATOR);   
   lv_obj_align_to(chat_progress_arc, start_chat_butn, LV_ALIGN_CENTER, 0, 0);

   // Manually update the label for the first time
   // chat_arc_label = lv_label_create(chat_progress_arc);
   // lv_obj_add_style(chat_arc_label, &style_label_default, LV_PART_MAIN);
   // lv_obj_set_style_text_font(chat_arc_label, &lv_font_montserrat_14, LV_PART_MAIN);
   // lv_label_set_text(chat_arc_label, "0%");
   // lv_obj_center(chat_arc_label);  
   
   // chat voice replay button
   chat_replay_butn = lv_button_create(parent);
   lv_obj_set_size(chat_replay_butn, 40, 40);
   lv_obj_set_style_radius(chat_replay_butn, 20, LV_PART_MAIN);
   lv_obj_add_style(chat_replay_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_set_style_border_color(chat_replay_butn, lv_palette_lighten(LV_PALETTE_TEAL, 2), LV_PART_MAIN);
   lv_obj_add_event_cb(chat_replay_butn, butn_event_cb, LV_EVENT_ALL, NULL); 
   lv_obj_align_to(chat_replay_butn, start_chat_butn, LV_ALIGN_OUT_LEFT_MID, -78, 2);

   chat_replay_symbol_label = lv_label_create(chat_replay_butn);
   lv_obj_center(chat_replay_symbol_label);
   lv_obj_set_style_text_font(chat_replay_symbol_label, &lv_font_montserrat_20, LV_PART_MAIN);
   lv_obj_set_style_text_align(chat_replay_symbol_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(chat_replay_symbol_label, lv_palette_lighten(LV_PALETTE_TEAL, 2), LV_PART_MAIN);   
   lv_label_set_text(chat_replay_symbol_label, LV_SYMBOL_PLAY);     
   
   lv_obj_t *chat_replay_label = lv_label_create(parent);
   lv_obj_set_style_text_font(chat_replay_label, &lv_font_montserrat_16, LV_PART_MAIN);
   lv_obj_set_style_text_align(chat_replay_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(chat_replay_label, lv_color_white(), LV_PART_MAIN);   
   lv_label_set_text(chat_replay_label, "Replay");    
   lv_obj_align_to(chat_replay_label, chat_replay_butn, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);   
   
   // Chat home button
   chat_home_butn = lv_button_create(parent);
   lv_obj_set_size(chat_home_butn, 44, 44);
   lv_obj_align(chat_home_butn, LV_ALIGN_BOTTOM_MID, 0, -15);
   lv_obj_add_style(chat_home_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_add_event_cb(chat_home_butn, butn_event_cb, LV_EVENT_ALL, NULL); 

   lv_obj_t *chat_home_butn_lbl = lv_label_create(chat_home_butn);
   lv_obj_center(chat_home_butn_lbl);
   lv_obj_set_style_text_font(chat_home_butn_lbl, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_align(chat_home_butn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(chat_home_butn_lbl, lv_palette_lighten(LV_PALETTE_CYAN, 1), LV_PART_MAIN);   
   lv_label_set_text(chat_home_butn_lbl, LV_SYMBOL_HOME);   
 
   // Timer with lambda function to display wifi connection status
   chat_timer = lv_timer_create([](lv_timer_t *timer) {
      int8_t chat_prog;
      if(lv_tileview_get_tile_act(tileview) == tv_page4) {  // only active on this page
         // Update WiFi connection
         updateWifiLabel(chat_wifi_label);

         // Update battery SOC
         updateBatteryLabel(chat_battery_label);   
         
         // update the replay label
         if(updateReplayLabel != REPLAY_UPDATE_NONE) {
            if(updateReplayLabel == REPLAY_UPDATE_STOP) {
               lv_label_set_text(chat_replay_symbol_label, LV_SYMBOL_STOP);
            }
            else if(updateReplayLabel == REPLAY_UPDATE_PLAY) {
               lv_label_set_text(chat_replay_symbol_label, LV_SYMBOL_PLAY);
            }
            lv_obj_invalidate(chat_replay_symbol_label);
            updateReplayLabel = REPLAY_UPDATE_NONE;
         }

         // Update playWav progress
         if(audio.isWavPlaying()) {
            chat_prog = audio.getWavPlayProgress();
            if(chat_prog > 98) chat_prog = 100;
            updateChatProgress(chat_prog);
         }
      }
   }, 100, NULL);     
}


/********************************************************************
 * @brief Update the Chatbot button label with progress percent.
 * @param progress - 0-100%. If -1, set label text with "Start".
 */
void updateChatProgress(int8_t progress)
{
   char buf[10];

   if(progress < 0) {   
      progress = 0;
      strcpy(buf, "Start");
   }
   else {
      sprintf(buf, "%d%%", progress);
   }
   lv_arc_set_value(chat_progress_arc, progress);   
   lv_label_set_text(start_chat_butn_label, buf);   
}


/********************************************************************
 * @brief Update the translate button label with a progress percent.
 * @param progress - 0-100%. If -1, set label text with "Start".
 */
void updateTranslateProgress(int8_t progress)
{
   char buf[10];

   if(progress < 0) {
      progress = 0;
      strcpy(buf, "Start");
   }
   else {
      sprintf(buf, "%d%%", progress);
   }
   lv_arc_set_value(xlate_progress_arc, progress);      
   lv_label_set_text(start_xlate_butn_label, buf);   
}


/********************************************************************
 * GUI Page 5 - Translate page
 */
void gui_page5(lv_obj_t *parent)
{
   // Translate page title
   lv_obj_t *xlate_title_label = lv_label_create(parent);
   lv_obj_set_size(xlate_title_label, 160, 36);
   lv_obj_add_style(xlate_title_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(xlate_title_label, 0, LV_PART_MAIN);
   lv_label_set_text(xlate_title_label, "TRANSLATE");
   lv_obj_set_style_text_color(xlate_title_label, lv_palette_lighten(LV_PALETTE_DEEP_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(xlate_title_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(xlate_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(xlate_title_label, LV_ALIGN_TOP_MID, 0, 2); 

   // WiFi label
   xlate_wifi_label = lv_label_create(parent);
   lv_obj_add_style(xlate_wifi_label, &style_wifi_label, LV_PART_MAIN);   
   lv_label_set_text(xlate_wifi_label, LV_SYMBOL_WIFI); 
   lv_obj_align(xlate_wifi_label, LV_ALIGN_TOP_LEFT, 10, 5);

   // Battery SOC label
   xlate_battery_label = lv_label_create(parent);
   lv_obj_add_style(xlate_battery_label, &style_battery_label, LV_PART_MAIN);     
   lv_label_set_text(xlate_battery_label, LV_SYMBOL_BATTERY_3); 
   lv_obj_align(xlate_battery_label, LV_ALIGN_TOP_RIGHT, -10, 5); 
   
   // Translate home button
   xlate_home_butn = lv_button_create(parent);
   lv_obj_set_size(xlate_home_butn, 42, 42);
   lv_obj_align(xlate_home_butn, LV_ALIGN_BOTTOM_MID, 0, -2);
   lv_obj_add_style(xlate_home_butn, &style_home_butn_default, LV_PART_MAIN);
   lv_obj_set_style_radius(xlate_home_butn, 21, LV_PART_MAIN);
   lv_obj_add_event_cb(xlate_home_butn, butn_event_cb, LV_EVENT_ALL, NULL); 

   lv_obj_t *xlate_home_butn_lbl = lv_label_create(xlate_home_butn);
   lv_obj_center(xlate_home_butn_lbl);
   lv_obj_set_style_text_font(xlate_home_butn_lbl, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_text_align(xlate_home_butn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_color(xlate_home_butn_lbl, lv_palette_lighten(LV_PALETTE_CYAN, 1), LV_PART_MAIN);   
   lv_label_set_text(xlate_home_butn_lbl, LV_SYMBOL_HOME);     

   // Start Translate button. 
   start_xlate_butn = lv_imagebutton_create(parent);
   lv_obj_set_size(start_xlate_butn, 48, 48);   
   lv_obj_add_style(start_xlate_butn, &icon_style_pressed, LV_STATE_PRESSED);
   lv_imagebutton_set_src(start_xlate_butn, LV_IMAGEBUTTON_STATE_RELEASED,           
               &icon_translate_48_cyan, &icon_translate_48_cyan, &icon_translate_48_cyan); 
   lv_obj_align(start_xlate_butn, LV_ALIGN_CENTER, 0, -48);
   lv_obj_add_event_cb(start_xlate_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   start_xlate_butn_label = lv_label_create(parent);
   lv_obj_set_size(start_xlate_butn_label, 130, 36);   
   lv_obj_add_style(start_xlate_butn_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_align(start_xlate_butn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_label_set_text(start_xlate_butn_label, "Start");
   lv_obj_set_style_text_font(start_xlate_butn_label, &lv_font_montserrat_16, LV_PART_MAIN);   
   lv_obj_align_to(start_xlate_butn_label, start_xlate_butn, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

   // Create a progress Arc
   xlate_progress_arc = lv_arc_create(parent);
   lv_obj_set_size(xlate_progress_arc, 76, 76);
   lv_arc_set_rotation(xlate_progress_arc, 135);
   lv_arc_set_bg_angles(xlate_progress_arc, 0, 270);
   lv_arc_set_range(xlate_progress_arc, 0, 100);
   lv_arc_set_value(xlate_progress_arc, 0);
   lv_obj_remove_style(xlate_progress_arc, NULL, LV_PART_KNOB);
   lv_obj_remove_flag(xlate_progress_arc, LV_OBJ_FLAG_CLICKABLE);   
   // Set the arc thickness (line width in pixels)
   lv_obj_set_style_arc_width(xlate_progress_arc, 5, LV_PART_INDICATOR);  // indicator arc
   lv_obj_set_style_arc_width(xlate_progress_arc, 5, LV_PART_MAIN);       // background arc
   lv_obj_set_style_arc_color(xlate_progress_arc, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
   lv_obj_set_style_arc_color(xlate_progress_arc, lv_palette_lighten(LV_PALETTE_CYAN, 2), LV_PART_INDICATOR);    
   lv_obj_align(xlate_progress_arc, LV_ALIGN_CENTER, 0, -48);

   /**
    * @brief Translate FROM roller
    */
   lv_obj_t *xf_roller_label = lv_label_create(parent);
   lv_obj_set_size(xf_roller_label, 110, 30);
   lv_obj_add_style(xf_roller_label, &style_label_default, LV_PART_MAIN);
   lv_obj_align(xf_roller_label, LV_ALIGN_LEFT_MID, 5, -60); 
   lv_obj_set_style_text_align(xf_roller_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);   
   lv_label_set_text(xf_roller_label, "FROM:");

   // make roller options string 
   char roller_ops[NUM_LANGUAGES * 20];   // array for language names
   strcpy(roller_ops, LanguageArray[0].language_name);
   for(int i=1; i<NUM_LANGUAGES; i++) {
      strcat(roller_ops, "\n");
      strcat(roller_ops, LanguageArray[i].language_name);
   }

   xfrom_roller = lv_roller_create(parent);
   lv_obj_set_width(xfrom_roller, 110);   
   lv_roller_set_options(xfrom_roller,
                  (char *)&roller_ops,
                  LV_ROLLER_MODE_INFINITE);

   lv_roller_set_visible_row_count(xfrom_roller, 5);
   lv_roller_set_selected(xfrom_roller, 1, LV_ANIM_OFF); // default 'from' = English
   lv_obj_align_to(xfrom_roller, xf_roller_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
   lv_obj_add_event_cb(xfrom_roller, roller_event_cb, LV_EVENT_ALL, NULL);  

   /**
    * @brief Translate TO roller
    */
   lv_obj_t *xt_roller_label = lv_label_create(parent);
   lv_obj_set_size(xt_roller_label, 110, 30);
   lv_obj_add_style(xt_roller_label, &style_label_default, LV_PART_MAIN);
   lv_obj_align(xt_roller_label, LV_ALIGN_RIGHT_MID, -5, -60); 
   lv_obj_set_style_text_align(xt_roller_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);     
   lv_label_set_text(xt_roller_label, "TO:");   

   xto_roller = lv_roller_create(parent);
   lv_obj_set_width(xto_roller, 110);    
   lv_roller_set_options(xto_roller,
                  (char *)&roller_ops,
                  LV_ROLLER_MODE_INFINITE);

   lv_roller_set_visible_row_count(xto_roller, 5);
   lv_roller_set_selected(xto_roller, 10, LV_ANIM_OFF);   // default 'to' = Spanish
   lv_obj_align_to(xto_roller, xt_roller_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
   lv_obj_add_event_cb(xto_roller, roller_event_cb, LV_EVENT_ALL, NULL);     

   /**
    * @brief Make a language from/to swap button
    */
   lang_swap_butn = lv_button_create(parent);
   lv_obj_set_size(lang_swap_butn, 42, 42);
   lv_obj_add_style(lang_swap_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(lang_swap_butn, &style_butn_pressed, LV_STATE_PRESSED);     
   lv_obj_set_style_radius(lang_swap_butn, 21, LV_PART_MAIN);   
   lv_obj_align(lang_swap_butn, LV_ALIGN_CENTER, 0, 38);   
   lv_obj_add_event_cb(lang_swap_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_set_style_pad_all(lang_swap_butn, 0, LV_PART_MAIN);   

   lv_obj_t *swap_butn_label = lv_label_create(lang_swap_butn);
   lv_obj_set_size(swap_butn_label, 30, 30);
   lv_obj_add_style(swap_butn_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(swap_butn_label, &lv_font_montserrat_22, LV_PART_MAIN);   
   lv_obj_center(swap_butn_label); 
   lv_obj_set_style_pad_top(swap_butn_label, 2, LV_PART_MAIN);    
   lv_obj_set_style_text_align(swap_butn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);   
   lv_label_set_text(swap_butn_label, LV_SYMBOL_LOOP);


   // Timer with lambda function to display wifi connection status
   xlate_timer = lv_timer_create([](lv_timer_t *timer) {
      if(lv_tileview_get_tile_act(tileview) == tv_page5) {  // only active on this page
         // Update WiFi connection
         updateWifiLabel(xlate_wifi_label);

         // Update battery SOC
         updateBatteryLabel(xlate_battery_label);   
         
         // // update the replay label
         // if(updateReplayLabel != REPLAY_UPDATE_NONE) {
         //    if(updateReplayLabel == REPLAY_UPDATE_STOP) {
         //       lv_label_set_text(chat_replay_symbol_label, LV_SYMBOL_STOP);
         //    }
         //    else if(updateReplayLabel == REPLAY_UPDATE_PLAY) {
         //       lv_label_set_text(chat_replay_symbol_label, LV_SYMBOL_PLAY);
         //    }
         //    lv_obj_invalidate(chat_replay_symbol_label);
         //    updateReplayLabel = REPLAY_UPDATE_NONE;
         // }

         // // Update playWav progress
         // if(audio.isWavPlaying()) {
         //    chat_prog = audio.getWavPlayProgress();
         //    if(chat_prog > 98) chat_prog = 100;
         //    updateChatProgress(chat_prog);
         // }
      }
   }, 100, NULL);        
}


/********************************************************************
 * @brief Roller event handler
 */
void roller_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * obj = lv_event_get_target_obj(e);
   if(code == LV_EVENT_VALUE_CHANGED) {
      char buf[32];
      lv_roller_get_selected_str(obj, buf, sizeof(buf));
      // Serial.printf("Selected language: %s\n", buf);
   }
}


/********************************************************************
 * @brief Return index of the from or to roller.
 */
uint8_t getLangIndex(uint8_t roller_id)
{
   if(roller_id == ROLLER_FROM) 
      return lv_roller_get_selected(xfrom_roller);
   else 
      return lv_roller_get_selected(xto_roller);
}


/********************************************************************
 * Load STT text into speak text label
 */
void setXFromText(char *labtext, uint8_t lang_sel_from)
{
   char buf[36];
   // lv_obj_set_style_text_font(translate_from_label, LanguageArray[lang_sel_from].lang_font, LV_PART_MAIN); // set desired font
   // lv_label_set_text(translate_from_label, labtext);  // set label text
   // strcpy(buf, "Translate From: ");
   // strncat(buf, LanguageArray[lang_sel_from].language_name, sizeof(buf)-strlen(buf));
   // lv_label_set_text(xfrom_hdr_label, buf);
   // // show STT text response - scroll to page 3
   // lv_obj_set_tile(tileview, tv_demo_page3, LV_ANIM_OFF);   // default starting page
   // lv_force_refresh(translate_from_label);   // force repaint of text area
}


/********************************************************************
 * Load STT text into speak text label
 */
void setXToText(char *labtext, uint8_t lang_sel_to)
{ 
   char buf[36];
      // lv_obj_set_style_text_font(translate_to_label, &noto_ru_18, LV_PART_MAIN); //lv_font_montserrat_18
   // lv_obj_set_style_text_font(translate_to_label, LanguageArray[lang_sel_to].lang_font, LV_PART_MAIN);
   //       // Serial.printf("labtext=%s\n", labtext);
   // lv_label_set_text(translate_to_label, labtext);
   // strcpy(buf, "Translate To: ");
   // strncat(buf, LanguageArray[lang_sel_to].language_name, sizeof(buf)-strlen(buf));
   // lv_label_set_text(xto_hdr_label, buf);   
   // // show Translated text response - scroll to page 3   
   // lv_force_refresh(translate_to_label);  
}


/********************************************************************
 * GUI Page 6 - Audio page
 */
void gui_page6(lv_obj_t *parent)
{
  
}


/********************************************************************
 * GUI Page 7 - Intercom page
 */
void gui_page7(lv_obj_t *parent)
{
  
}


/********************************************************************
 * GUI Page 8 - Settings
 */
void gui_page8(lv_obj_t *parent)
{
  
}


/********************************************************************
 * Update the display of current date & time.
 */
void refreshDateTime(void) 
{
   now = sys_utils.RTCgetDateTime(); 
   if(now.Second() == last_sec) return;

   last_sec = now.Second();
   char buf[20];
   char ampm[6];
   uint8_t hour = now.Hour();

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
   if((now.Second() % 2) == 0) {
      sprintf(buf, "%02d:%02d", hour, now.Minute());   
   } else {
      sprintf(buf, "%02d;%02d", hour, now.Minute());   // using ';' as blank ':'
   }
   lv_label_set_text(time_label, buf);
   lv_label_set_text(ampm_label, ampm); 

   snprintf(buf, sizeof(buf), "%s, %s %02d, %d", 
         weekdays[now.DayOfWeek()], months[now.Month()-1], now.Day(), now.Year());
   lv_label_set_text(date_label, buf);
}


/********************************************************************
 * @brief Update a wifi label symbol color 
 */
void updateWifiLabel(lv_obj_t *lbl)
{
   // WiFi connected?
   if(WiFi.status() == WL_CONNECTED) {
      lv_obj_set_style_text_color(lbl, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN);  
   } else {
      lv_obj_set_style_text_color(lbl, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN); 
   }
}


/********************************************************************
 * @brief Update a battery SOC label.
 */
void updateBatteryLabel(lv_obj_t *batlbl)
{
   // Update battery SOC
   system_power_t *spwr = sys_utils.getPowerInfo();
   if(spwr->state_of_charge > 85) {
      lv_obj_set_style_text_color(batlbl, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN);   
      lv_label_set_text(batlbl, LV_SYMBOL_BATTERY_FULL); 
   }
   else if(spwr->state_of_charge < 85 && spwr->state_of_charge > 60) {
      lv_obj_set_style_text_color(batlbl, lv_palette_darken(LV_PALETTE_BLUE, 1), LV_PART_MAIN);   
      lv_label_set_text(batlbl, LV_SYMBOL_BATTERY_3); 
   }
   else if(spwr->state_of_charge < 60 && spwr->state_of_charge > 40) {
      lv_obj_set_style_text_color(batlbl, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);   
      lv_label_set_text(batlbl, LV_SYMBOL_BATTERY_2); 
   }     
   else if(spwr->state_of_charge < 40 && spwr->state_of_charge > 20) {
      lv_obj_set_style_text_color(batlbl, lv_palette_lighten(LV_PALETTE_YELLOW, 1), LV_PART_MAIN);   
      lv_label_set_text(batlbl, LV_SYMBOL_BATTERY_1); 
   }     
   else if(spwr->state_of_charge < 20) {
      lv_obj_set_style_text_color(batlbl, lv_palette_lighten(LV_PALETTE_RED, 1), LV_PART_MAIN);   
      lv_label_set_text(batlbl, LV_SYMBOL_BATTERY_EMPTY); 
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
      if(butn == clock_home_butn || butn == alarm_home_butn || 
            butn == chat_home_butn || butn == xlate_home_butn) {
         lv_obj_set_tile(tileview, tv_page1, LV_ANIM_ON);   // default starting page
      }
      else if(butn == start_chat_butn) {
         if(WiFi.status() == WL_CONNECTED) {
            gui_event = GUI_EVENT_CHAT_REQ;
         } else {
            openMessageBox(LV_SYMBOL_WARNING " WiFi Error!", "WiFi not connected. Check WiFi SSID and Password credentials.", "OK", "");
         }
      }      
      else if(butn == chat_replay_butn) {
         if(!audio.isWavPlaying()) {
            updateReplayLabel = REPLAY_UPDATE_STOP;   
            updateChatProgress(0);         
            audio.playWavFile(CHAT_RESPONSE_FILENAME, 50, false, playwav_callback);         
         } else {
            audio.sendPlayWavCommand(PLAY_WAV_STOP);
         }
      }
      else if(butn == lang_swap_butn) {
         uint32_t xto = lv_roller_get_selected(xto_roller);
         uint32_t xfrom = lv_roller_get_selected(xfrom_roller);
         lv_roller_set_selected(xto_roller, xfrom, LV_ANIM_ON);       
         lv_roller_set_selected(xfrom_roller, xto, LV_ANIM_ON);               
      }      
      else if(butn == start_xlate_butn) {
         if(strcmp(lv_label_get_text(start_xlate_butn_label), "Start") == 0) {
            lv_label_set_text(start_xlate_butn_label, "0%");
            gui_event = GUI_EVENT_TRANSLATE;
         } else {
            lv_label_set_text(start_xlate_butn_label, "Start");
            gui_event = GUI_EVENT_STOP;            
         }
      }
   }
}


/********************************************************************
 * @brief Callback from playWavFile when function has completed.
 * @param sym_type - 1 = LV_SYMBOL_PLAY, 2 = LV_SYMBOL_STOP
 */
static void playwav_callback(uint8_t sym_type) 
{
   updateReplayLabel = sym_type;   
}


/********************************************************************
 *  @brief Check if GUI is reporting any events (button clicks, etc.)
 *  @return GUI Event type - see 'gui_event_type'.
 */
int16_t getGUIevents(void)
{
   static int16_t gev = GUI_EVENT_NONE; 

   gev = gui_event;
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
   lv_style_set_bg_color(&style_butn_released, lv_color_black());
   lv_style_set_border_width(&style_butn_released, 2);
   lv_style_set_border_color(&style_butn_released, lv_palette_lighten(LV_PALETTE_CYAN, 1));
   lv_style_set_shadow_width(&style_butn_released, 0);
   lv_style_set_outline_width(&style_butn_released, 0);       // no outline 
   lv_style_set_pad_all(&style_butn_released, 1);

   /**
   *  @brief Init the start button pressed style
   */
   lv_style_init(&style_butn_pressed);
   lv_style_set_bg_color(&style_butn_pressed, lv_color_black());
   lv_style_set_border_width(&style_butn_pressed, 0);
   lv_style_set_pad_all(&style_butn_pressed, 0);   
   lv_style_set_shadow_width(&style_butn_pressed, 0); 

   lv_style_set_outline_pad(&style_butn_pressed, 1);
   lv_style_set_outline_width(&style_butn_pressed, 5);
   lv_style_set_outline_color(&style_butn_pressed, lv_palette_lighten(LV_PALETTE_CYAN, 2)); 
   lv_style_set_outline_opa(&style_butn_pressed, LV_OPA_COVER);

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
    *  @brief Init the HOME button default style
    */
   lv_style_init(&style_home_butn_default);
   lv_style_set_bg_opa(&style_home_butn_default, LV_OPA_100);
   lv_style_set_radius(&style_home_butn_default, 8);
   lv_style_set_bg_color(&style_home_butn_default, lv_color_black());
   lv_style_set_border_width(&style_home_butn_default, 2);
   lv_style_set_border_color(&style_home_butn_default, lv_palette_lighten(LV_PALETTE_CYAN, 1));
   lv_style_set_shadow_width(&style_home_butn_default, 0);
   lv_style_set_outline_width(&style_home_butn_default, 0);       // no outline 
   lv_style_set_pad_all(&style_home_butn_default, 1);

   /**
    * @brief WiFi Label Style
    */
   lv_style_init(&style_wifi_label);
   lv_style_set_bg_opa(&style_wifi_label, LV_OPA_100);
   lv_style_set_radius(&style_wifi_label, 6);
   lv_style_set_text_font(&style_wifi_label, &lv_font_montserrat_26);  
   lv_style_set_text_align(&style_wifi_label, LV_TEXT_ALIGN_CENTER);
   lv_style_set_text_color(&style_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 3)); 
   lv_style_set_bg_color(&style_wifi_label, lv_color_black());
   lv_style_set_border_width(&style_wifi_label, 0);
   lv_style_set_shadow_width(&style_wifi_label, 0);
   lv_style_set_outline_width(&style_wifi_label, 0);       // no outline 
   lv_style_set_pad_all(&style_wifi_label, 1);   

   /**
    * @brief Battery Label Style
    */
   lv_style_init(&style_battery_label);
   lv_style_set_bg_opa(&style_battery_label, LV_OPA_100);
   lv_style_set_radius(&style_battery_label, 6);
   lv_style_set_text_font(&style_battery_label, &lv_font_montserrat_26);  
   lv_style_set_text_align(&style_battery_label, LV_TEXT_ALIGN_CENTER);
   lv_style_set_text_color(&style_battery_label, lv_palette_darken(LV_PALETTE_GREY, 2)); 
   lv_style_set_bg_color(&style_battery_label, lv_color_black());
   lv_style_set_border_width(&style_battery_label, 0);
   lv_style_set_shadow_width(&style_battery_label, 0);
   lv_style_set_outline_width(&style_battery_label, 0);       // no outline 
   lv_style_set_pad_all(&style_battery_label, 1);    
   
   /**
    * @brief Icon image button style
    */
   lv_style_init(&icon_style_pressed);
   lv_style_set_border_width(&icon_style_pressed, 2);
   lv_style_set_border_color(&icon_style_pressed, lv_palette_main(LV_PALETTE_PINK));
   lv_style_set_image_recolor_opa(&icon_style_pressed, LV_OPA_20);
   lv_style_set_image_recolor(&icon_style_pressed, lv_color_black());
   lv_style_set_transform_width(&icon_style_pressed, 10);   
}


// ----------------------
// One shared event callback for all icons
// ----------------------
void launcher_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    // Recover which app this icon represents
    int icon_id = (int)(uintptr_t)lv_obj_get_user_data(obj);

    switch (icon_id) {
        case ICON_CLOCK:
            lv_obj_set_tile(tileview, tv_page2, LV_ANIM_ON);   // clock page
            break;

        case ICON_ALARM:
            lv_obj_set_tile(tileview, tv_page3, LV_ANIM_ON);   // alarm page
            break;     

        case ICON_CHATBOT:
            lv_obj_set_tile(tileview, tv_page4, LV_ANIM_ON); 
            break;      

        case ICON_TRANSLATE:
            lv_obj_set_tile(tileview, tv_page5, LV_ANIM_ON); 
            break;             
            
        case ICON_AUDIO:
            lv_obj_set_tile(tileview, tv_page6, LV_ANIM_ON); 
            break;        

        case ICON_INTERCOM:
            lv_obj_set_tile(tileview, tv_page7, LV_ANIM_ON); 
            break;                  

        case ICON_SETTINGS:
            lv_obj_set_tile(tileview, tv_page8, LV_ANIM_ON);
            break;            

        default:
            lv_obj_set_tile(tileview, tv_page1, LV_ANIM_ON);   // default return to home page
            break;
    }
}


/********************************************************************
 * @brief Message Box popup
 */
void openMessageBox(const char *title_text, const char *body_text, const char *btn1_text, const char *btn2_text)
{
   msgbox1 = lv_msgbox_create(NULL);

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
      lv_obj_add_event_cb(btn, mbox_event_cb, LV_EVENT_CLICKED, NULL);   
   }
   if(strlen(btn2_text) > 0) {
      btn = lv_msgbox_add_footer_button(msgbox1, btn2_text);
      lv_obj_add_event_cb(btn, mbox_event_cb, LV_EVENT_CLICKED, NULL);   
   }   
}


/********************************************************************
 * @brief Message Box button event handler
 */
static void mbox_event_cb(lv_event_t * e)
{
   lv_obj_t * btn = lv_event_get_target_obj(e);
   lv_obj_t * label = lv_obj_get_child(btn, 0);
   char *btn_text = lv_label_get_text(label);
   // Serial.printf("Button %s clicked!", btn_text);

   if(strcmp(btn_text, "OK") == 0) {
      lv_msgbox_close(msgbox1);
   }
}