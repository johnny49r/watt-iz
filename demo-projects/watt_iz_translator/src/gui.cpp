/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
int16_t gui_event;

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
         "French", "fr-FR", "fr-FR-Wavenet-F", &noto_latin_18, 0.75 // she talks too fast!
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
   tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_ALL);
   tv_demo_page3 =  lv_tileview_add_tile(tileview, 1, 1, LV_DIR_VER);

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
   LV_IMAGE_DECLARE(lang_trans);

   // show demo info
   lv_obj_t *scrn1_demo_label = lv_label_create(parent);
   lv_obj_set_size(scrn1_demo_label, 320, 40);
   lv_obj_add_style(scrn1_demo_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1_demo_label, 0, LV_PART_MAIN);
   lv_label_set_text(scrn1_demo_label, "WATT-IZ DEMO PROJECTS");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_DEEP_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 5);      

   // Project demo image
   lv_obj_t * img1 = lv_image_create(parent);
   lv_image_set_src(img1, &lang_trans);
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
   lv_obj_set_size(scrn2_wifi_label, 54, 54);
   lv_obj_set_style_radius(scrn2_wifi_label, 27, LV_PART_MAIN);
   lv_obj_add_style(scrn2_wifi_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn2_wifi_label, 3, LV_PART_MAIN);
   lv_obj_set_style_border_color(scrn2_wifi_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);   
   lv_label_set_text(scrn2_wifi_label, LV_SYMBOL_WIFI);
   lv_obj_set_style_text_align(scrn2_wifi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_pad_top(scrn2_wifi_label, 12, LV_PART_MAIN);
   lv_obj_set_style_pad_left(scrn2_wifi_label, 2, LV_PART_MAIN);   
   lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn2_wifi_label, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_align(scrn2_wifi_label, LV_ALIGN_TOP_LEFT, 6, 6);     

   // Translate button
   translate_butn = lv_button_create(parent);
   lv_obj_set_size(translate_butn, 110, 40);
   lv_obj_align(translate_butn, LV_ALIGN_TOP_MID, 0, 10);
   lv_obj_add_style(translate_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(translate_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(translate_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_set_ext_click_area(translate_butn, 6);
   lv_obj_add_state(translate_butn, LV_STATE_DISABLED);

   translate_butn_label = lv_label_create(translate_butn);
   lv_obj_add_style(translate_butn_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(translate_butn_label, "Translate");
   lv_obj_center(translate_butn_label);

   // Create a progress Arc
   progress_arc = lv_arc_create(parent);
   lv_obj_set_size(progress_arc, 60, 60);
   lv_arc_set_rotation(progress_arc, 135);
   lv_arc_set_bg_angles(progress_arc, 0, 270);
   lv_arc_set_range(progress_arc, 0, 100);
   lv_arc_set_value(progress_arc, 0);
   lv_obj_remove_style(progress_arc, NULL, LV_PART_KNOB);
   lv_obj_remove_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);   
   // Set the arc thickness (line width in pixels)
   lv_obj_set_style_arc_width(progress_arc, 5, LV_PART_INDICATOR);  // indicator arc
   lv_obj_set_style_arc_width(progress_arc, 5, LV_PART_MAIN);       // background arc
   lv_obj_align(progress_arc, LV_ALIGN_TOP_RIGHT, -10, 6);
   lv_obj_add_event_cb(progress_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

   // Manually update the label for the first time
   // lv_obj_send_event(progress_arc, LV_EVENT_VALUE_CHANGED, NULL);

   arc_label = lv_label_create(progress_arc);
   lv_obj_add_style(arc_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(arc_label, &lv_font_montserrat_14, LV_PART_MAIN);
   lv_label_set_text(arc_label, "0%");
   lv_obj_center(arc_label);

   /**
    * @brief Translate FROM roller
    */
   lv_obj_t *xf_roller_label = lv_label_create(parent);
   lv_obj_set_size(xf_roller_label, 110, 30);
   lv_obj_add_style(xf_roller_label, &style_label_default, LV_PART_MAIN);
   lv_obj_align(xf_roller_label, LV_ALIGN_LEFT_MID, 5, -35); 
   lv_obj_set_style_text_align(xf_roller_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);   
   lv_label_set_text(xf_roller_label, "FROM:");

   // make roller options string 
   char roller_ops[NUM_LANGUAGES * 20];   // space to accum 'n' language names
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

   lv_roller_set_visible_row_count(xfrom_roller, 3);
   lv_roller_set_selected(xfrom_roller, 1, LV_ANIM_OFF); // default 'from' = English
   lv_obj_align_to(xfrom_roller, xf_roller_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
   lv_obj_add_event_cb(xfrom_roller, roller_event_cb, LV_EVENT_ALL, NULL);  

   /**
    * @brief Translate TO roller
    */
   lv_obj_t *xt_roller_label = lv_label_create(parent);
   lv_obj_set_size(xt_roller_label, 110, 30);
   lv_obj_add_style(xt_roller_label, &style_label_default, LV_PART_MAIN);
   lv_obj_align(xt_roller_label, LV_ALIGN_RIGHT_MID, -5, -35); 
   lv_obj_set_style_text_align(xt_roller_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);     
   lv_label_set_text(xt_roller_label, "TO:");   

   xto_roller = lv_roller_create(parent);
   lv_obj_set_width(xto_roller, 110);    
   lv_roller_set_options(xto_roller,
                  (char *)&roller_ops,
                  LV_ROLLER_MODE_INFINITE);

   lv_roller_set_visible_row_count(xto_roller, 3);
   lv_roller_set_selected(xto_roller, 6, LV_ANIM_OFF);   // default 'to' = Spanish
   lv_obj_align_to(xto_roller, xt_roller_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
   lv_obj_add_event_cb(xto_roller, roller_event_cb, LV_EVENT_ALL, NULL);     

   /**
    * @brief Make a language from/to swap button
    */
   lang_swap_butn = lv_button_create(parent);
   lv_obj_set_size(lang_swap_butn, 50, 50);
   lv_obj_set_style_radius(lang_swap_butn, 25, LV_PART_MAIN);   
   lv_obj_align(lang_swap_butn, LV_ALIGN_CENTER, 0, 35);
   lv_obj_add_style(lang_swap_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(lang_swap_butn, &style_butn_pressed, LV_STATE_PRESSED);     
   lv_obj_add_event_cb(lang_swap_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_set_style_pad_all(lang_swap_butn, 0, LV_PART_MAIN);   

   lv_obj_t *swap_butn_label = lv_label_create(lang_swap_butn);
   lv_obj_set_size(swap_butn_label, 30, 30);
   lv_obj_add_style(swap_butn_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(swap_butn_label, &lv_font_montserrat_26, LV_PART_MAIN);
   lv_obj_set_style_pad_top(swap_butn_label, 0, LV_PART_MAIN);    
   lv_obj_center(swap_butn_label); 
   lv_obj_set_style_text_align(swap_butn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);   
   lv_label_set_text(swap_butn_label, LV_SYMBOL_LOOP);

   /**
    * @brief Timer with lambda function to display wifi connection status
    */
   talk_timer = lv_timer_create([](lv_timer_t *timer) {
      static bool blink = false;
      if(lv_tileview_get_tile_active(tileview) == tv_demo_page2) {
         if(WiFi.status() == WL_CONNECTED) {
            lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_MAIN);
            lv_obj_clear_state(translate_butn, LV_STATE_DISABLED);
         } else {
            blink ^= true;
            lv_obj_add_state(translate_butn, LV_STATE_DISABLED);
            if(blink)
               lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
            else 
               lv_obj_set_style_text_color(scrn2_wifi_label, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
         }
      }
   }, 500, NULL);    
}


/********************************************************************
 * Demo Page 3 - show results of translation
 */
void demo_page3(lv_obj_t *parent)
{
   xfrom_hdr_label = lv_label_create(parent);
   lv_obj_set_size(xfrom_hdr_label, 280, 30);
   lv_obj_add_style(xfrom_hdr_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_align(xfrom_hdr_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
   lv_obj_align(xfrom_hdr_label, LV_ALIGN_TOP_LEFT, 0, 0); 
   lv_label_set_text(xfrom_hdr_label, "Translate From:");

   lv_obj_t *from_cont = lv_obj_create(parent);
   lv_obj_set_size(from_cont, 310, 85);
   lv_obj_set_scroll_dir(from_cont, LV_DIR_VER);                 // vertical scroll
   lv_obj_set_scrollbar_mode(from_cont, LV_SCROLLBAR_MODE_AUTO); // show when scrolling
   lv_obj_set_style_pad_all(from_cont, 0, LV_PART_MAIN);
   lv_obj_set_style_bg_color(from_cont, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(from_cont, LV_OPA_0, LV_PART_MAIN);
   lv_obj_set_style_border_width(from_cont, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(from_cont, lv_color_white(), LV_PART_MAIN);   
   lv_obj_set_scroll_snap_y(from_cont, LV_SCROLL_SNAP_NONE);   
   lv_obj_align_to(from_cont, xfrom_hdr_label, LV_ALIGN_OUT_BOTTOM_MID, 16, 0);   

   translate_from_label = lv_label_create(from_cont);
   lv_obj_set_width(translate_from_label, 308);
   lv_obj_add_style(translate_from_label, &style_label_default, LV_PART_MAIN);
   
   // Enable text wrap
   lv_label_set_long_mode(translate_from_label, LV_LABEL_LONG_WRAP);
   lv_label_set_text(translate_from_label, "");

   xto_hdr_label = lv_label_create(parent);
   lv_obj_set_size(xto_hdr_label, 280, 30);
   lv_obj_add_style(xto_hdr_label, &style_label_default, LV_PART_MAIN);  
   lv_obj_set_style_text_align(xto_hdr_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);    
   lv_obj_align_to(xto_hdr_label, from_cont, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0); 
   lv_label_set_text(xto_hdr_label, "Translate To:");

   // Repeat translation button
   repeat_trans_butn = lv_button_create(parent);
   lv_obj_set_size(repeat_trans_butn, 30, 30);
   lv_obj_add_style(repeat_trans_butn, &style_butn_released, LV_STATE_DEFAULT);
   lv_obj_add_style(repeat_trans_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   lv_obj_add_event_cb(repeat_trans_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);
   lv_obj_align_to(repeat_trans_butn, xto_hdr_label, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

   lv_obj_t *repeat_trans_label = lv_label_create(repeat_trans_butn);   
   lv_obj_add_style(repeat_trans_label, &style_label_default, LV_PART_MAIN);    
   lv_obj_set_style_text_align(repeat_trans_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); 
   lv_obj_center(repeat_trans_label);
   lv_label_set_text(repeat_trans_label, LV_SYMBOL_PLAY);

   lv_obj_t *to_cont = lv_obj_create(parent);
   lv_obj_set_size(to_cont, 310, 85);
   lv_obj_set_scroll_dir(to_cont, LV_DIR_VER);                 // vertical scroll
   lv_obj_set_scrollbar_mode(to_cont, LV_SCROLLBAR_MODE_AUTO); // show when scrolling
   lv_obj_set_style_pad_all(to_cont, 0, LV_PART_MAIN);
   lv_obj_set_style_bg_color(to_cont, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(to_cont, LV_OPA_0, LV_PART_MAIN);
   lv_obj_set_style_border_width(to_cont, 1, LV_PART_MAIN);
   lv_obj_set_style_border_color(to_cont, lv_color_white(), LV_PART_MAIN);   
   lv_obj_set_scroll_snap_y(to_cont, LV_SCROLL_SNAP_NONE);   
   lv_obj_align_to(to_cont, xto_hdr_label, LV_ALIGN_OUT_BOTTOM_MID, 16, 0);      

   translate_to_label = lv_label_create(to_cont);
   lv_obj_set_width(translate_to_label, 308);
   lv_obj_add_style(translate_to_label, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(translate_to_label, "");
   
   // Enable text wrap
   lv_label_set_long_mode(translate_to_label, LV_LABEL_LONG_WRAP);

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
      Serial.printf("Selected language: %s\n", buf);
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
 * 
 */
void arc_event_cb(lv_event_t * e)
{
    lv_obj_t * arc = lv_event_get_target_obj(e);
}


/********************************************************************
 * 
 */
void setRecProgress(uint8_t progress)
{
   char buf[10];
   lv_arc_set_value(progress_arc, progress);
   sprintf(buf, "%d%", progress);
   lv_label_set_text(arc_label, buf);   
}


/********************************************************************
 * Load STT text into speak text label
 */
void setXFromText(char *labtext, uint8_t lang_sel_from)
{
   char buf[36];
   lv_obj_set_style_text_font(translate_from_label, LanguageArray[lang_sel_from].lang_font, LV_PART_MAIN); // set desired font
   lv_label_set_text(translate_from_label, labtext);  // set label text
   strcpy(buf, "Translate From: ");
   strncat(buf, LanguageArray[lang_sel_from].language_name, sizeof(buf)-strlen(buf));
   lv_label_set_text(xfrom_hdr_label, buf);
   // show STT text response - scroll to page 3
   lv_obj_set_tile(tileview, tv_demo_page3, LV_ANIM_OFF);   // default starting page
   lv_force_refresh(translate_from_label);   // force repaint of text area
}


/********************************************************************
 * Load STT text into speak text label
 */
void setXToText(char *labtext, uint8_t lang_sel_to)
{ 
   char buf[36];
      // lv_obj_set_style_text_font(translate_to_label, &noto_ru_18, LV_PART_MAIN); //lv_font_montserrat_18
   lv_obj_set_style_text_font(translate_to_label, LanguageArray[lang_sel_to].lang_font, LV_PART_MAIN);
         // Serial.printf("labtext=%s\n", labtext);
   lv_label_set_text(translate_to_label, labtext);
   strcpy(buf, "Translate To: ");
   strncat(buf, LanguageArray[lang_sel_to].language_name, sizeof(buf)-strlen(buf));
   lv_label_set_text(xto_hdr_label, buf);   
   // show Translated text response - scroll to page 3   
   lv_force_refresh(translate_to_label);  
}


/********************************************************************
 * @brief Force LVGL to repaint the object or screen and block until finished.
 */
void lv_force_refresh(lv_obj_t *_obj) 
{
   if(!_obj)                              // if _obj is NULL, refresh the current screen
      _obj = lv_screen_active(); 
   lv_obj_invalidate(_obj);               // or a specific object
   lv_display_refr_timer(NULL);           // run the refresh step immediately   
}


/********************************************************************
 * Load text into speak butn label
 */
void setSpeakButnText(const char *butn_text)
{
   lv_label_set_text(translate_butn_label, butn_text);
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);
   String label_text;

   if(code == LV_EVENT_CLICKED) {
      if(butn == translate_butn) {
         label_text = lv_label_get_text(translate_butn_label);  // current butn text
         if(label_text.equals("Translate")) {
            lv_label_set_text(translate_butn_label, "Stop");
            gui_event = GUI_EVENT_START_REC;
         } else {
            lv_label_set_text(translate_butn_label, "Translate");
            gui_event = GUI_EVENT_STOP_REC;            
         }
      }
      else if(butn == repeat_trans_butn) {   // play translated voice again?
         audio.playWavFile("/xlate_voice.wav", 60);
      }
      else if(butn == lang_swap_butn) {
         uint32_t xto = lv_roller_get_selected(xto_roller);
         uint32_t xfrom = lv_roller_get_selected(xfrom_roller);
         lv_roller_set_selected(xto_roller, xfrom, LV_ANIM_ON);       
         lv_roller_set_selected(xfrom_roller, xto, LV_ANIM_ON);               
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


