/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
#define DISPLAY_POINTS           512

// Graphics hardware library
TFT_eSPI tft = TFT_eSPI();
#if defined(USE_RESISTIVE_TOUCH_SCREEN)
   uint16_t calData[5] = { 360, 3400, 360, 3400, 7 }; // Example — use your own from calibration
#endif

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib

// FFT stuff
ESP32S3_FFT fft;


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
   tft.setSwapBytes(true);                // swap bytes for lvgl

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
   demo_page2(tv_demo_page2);               
   demo_page3(tv_demo_page3);              

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_ON);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   LV_IMAGE_DECLARE(Audio_Demo_Inverted_300x150_NoSpeckle);
   
   // show demo label
   lv_obj_t *scrn1_demo_label = lv_label_create(parent);
   lv_obj_set_size(scrn1_demo_label, 320, 40);
   lv_obj_add_style(scrn1_demo_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_border_width(scrn1_demo_label, 0, LV_PART_MAIN);
   lv_label_set_text(scrn1_demo_label, "WATT-IZ DEMO PROJECTS");
   lv_obj_set_style_text_color(scrn1_demo_label, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_font(scrn1_demo_label, &lv_font_montserrat_22, LV_PART_MAIN);
   lv_obj_set_style_text_align(scrn1_demo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_align(scrn1_demo_label, LV_ALIGN_TOP_MID, 0, 5);      

   // project icon image
   lv_obj_t * img1 = lv_image_create(parent);
   lv_image_set_src(img1, &Audio_Demo_Inverted_300x150_NoSpeckle);
   lv_obj_align_to(img1, scrn1_demo_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5); 

   lv_obj_t *swipe_label1 = lv_label_create(parent);
   lv_obj_set_size(swipe_label1, 120, 36);
   lv_obj_center(swipe_label1);
   lv_label_set_text(swipe_label1, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   lv_obj_set_style_text_color(swipe_label1, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_align(swipe_label1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(swipe_label1, &lv_font_montserrat_20, LV_PART_MAIN);   
   lv_obj_align(swipe_label1, LV_ALIGN_BOTTOM_MID, 0, 0);   

   // // Next button
   // next_butn = lv_button_create(parent);
   // lv_obj_set_size(next_butn, 80, 32);
   // lv_obj_align(next_butn, LV_ALIGN_BOTTOM_MID, 0, -10);
   // lv_obj_add_style(next_butn, &style_butn_released, LV_STATE_DEFAULT);
   // lv_obj_add_style(next_butn, &style_butn_pressed, LV_STATE_PRESSED);   
   // lv_obj_add_event_cb(next_butn, butn_event_cb, LV_EVENT_CLICKED, NULL);

   // lv_obj_t *next_butn_label = lv_label_create(next_butn);
   // lv_obj_add_style(next_butn_label, &style_label_default, LV_PART_MAIN);
   // lv_label_set_text(next_butn_label, "Next " LV_SYMBOL_RIGHT);
   // lv_obj_center(next_butn_label);   
}


/********************************************************************
 * Demo Page 2
 */
void demo_page2(lv_obj_t *parent)
{
   // Create tone ON/OFF button
   demo_butn1 = lv_btn_create(parent);
   lv_obj_set_size(demo_butn1, 100, 45);
   lv_obj_add_style(demo_butn1, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(demo_butn1, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);   
   lv_obj_set_style_radius(demo_butn1, 6, LV_PART_MAIN);   // make button a circle
   lv_obj_align(demo_butn1, LV_ALIGN_TOP_MID, 0, 5);   
   lv_obj_add_event_cb(demo_butn1, &butn_event_cb, LV_EVENT_CLICKED, NULL);   
   // Make this switch toggleable (keeps checked state)
   lv_obj_add_flag(demo_butn1, LV_OBJ_FLAG_CHECKABLE);   
   
   demo_label1 = lv_label_create(demo_butn1);
   lv_obj_center(demo_label1);
   lv_label_set_text(demo_label1, "Tone ON");

   // Create tone frequency slider
   tone_slider = lv_slider_create(parent);
   lv_obj_align_to(tone_slider, demo_butn1, LV_ALIGN_OUT_BOTTOM_MID, 0, 30); 
   lv_slider_set_range(tone_slider, 100, 3000);
   lv_slider_set_value(tone_slider, 1000, LV_PART_MAIN);
   lv_obj_add_event_cb(tone_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
   lv_obj_set_style_bg_color(tone_slider, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(tone_slider, LV_OPA_60, LV_PART_MAIN);
   lv_obj_set_ext_click_area(tone_slider, 15);

   // Create a label below the slider
   tone_slider_label = lv_label_create(parent);
   lv_obj_set_size(tone_slider_label, 150, 30);
   lv_obj_set_style_radius(tone_slider_label, 6, LV_PART_MAIN);
   lv_label_set_text(tone_slider_label, "Freq= 1000 Hz");
   lv_obj_set_style_text_align(tone_slider_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_bg_opa(tone_slider_label, LV_OPA_0, LV_PART_MAIN);
   // lv_obj_set_style_border_width(tone_slider_label, 1, LV_PART_MAIN);
   // lv_obj_set_style_border_color(tone_slider_label, lv_palette_lighten(LV_PALETTE_BLUE, 1), LV_PART_MAIN);
   lv_obj_set_style_text_color(tone_slider_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_align_to(tone_slider_label, tone_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);   

   // Create tone volume slider
   tone_vol_slider = lv_slider_create(parent);
   lv_obj_align_to(tone_vol_slider, tone_slider_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15); 
   lv_slider_set_range(tone_vol_slider, 0, 100);
   tone_volume = 50;   
   lv_slider_set_value(tone_vol_slider, tone_volume, LV_PART_MAIN);
   lv_obj_add_event_cb(tone_vol_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
   lv_obj_set_style_bg_color(tone_vol_slider, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);
   lv_obj_set_style_bg_opa(tone_vol_slider, LV_OPA_60, LV_PART_MAIN);
   lv_obj_set_ext_click_area(tone_vol_slider, 15);

   // Create a label below the slider
   tone_vol_slider_label = lv_label_create(parent);
   lv_obj_set_size(tone_vol_slider_label, 150, 30);
   lv_obj_set_style_radius(tone_vol_slider_label, 6, LV_PART_MAIN);
   lv_label_set_text(tone_vol_slider_label, "Volume= 50%");
   lv_obj_set_style_text_align(tone_vol_slider_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_bg_opa(tone_vol_slider_label, LV_OPA_0, LV_PART_MAIN);
   // lv_obj_set_style_border_width(tone_vol_slider_label, 1, LV_PART_MAIN);
   // lv_obj_set_style_border_color(tone_vol_slider_label, lv_palette_lighten(LV_PALETTE_BLUE, 1), LV_PART_MAIN);
   lv_obj_set_style_text_color(tone_vol_slider_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_align_to(tone_vol_slider_label, tone_vol_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);    

   lv_obj_t *swipe_label2 = lv_label_create(parent);
   lv_obj_set_size(swipe_label2, 120, 36);
   lv_obj_center(swipe_label2);
   lv_label_set_text(swipe_label2, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
   lv_obj_set_style_text_color(swipe_label2, lv_palette_lighten(LV_PALETTE_ORANGE, 3), LV_PART_MAIN);
   lv_obj_set_style_text_align(swipe_label2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
   lv_obj_set_style_text_font(swipe_label2, &lv_font_montserrat_20, LV_PART_MAIN);   
   lv_obj_align(swipe_label2, LV_ALIGN_BOTTOM_MID, 0, 0);   
  
}


/********************************************************************
 * Demo Page 3
 */
void demo_page3(lv_obj_t *parent)
{ 
   // Create a chart to display audio/FFT from the microphone
   chart = lv_chart_create(parent);      
   lv_obj_set_size(chart, 320, 165); //316, 120);    
   lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -256, 256);
   lv_obj_set_style_radius(chart, 3, LV_PART_MAIN);
   lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 0);
   lv_chart_set_type(chart, LV_CHART_TYPE_LINE);   // Show lines and points too
   lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);     // don't show scrollbars on non-scrolling pages
   lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

   ser1 = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);  
   ser2 = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_SECONDARY_Y);   

   // X axis as a scale aligned to the bottom 
   lv_obj_t *x_scale = lv_scale_create(parent);
   lv_scale_set_mode(x_scale, LV_SCALE_MODE_HORIZONTAL_TOP);
   lv_obj_set_size(x_scale, 315, 24);
   lv_scale_set_range(x_scale, 0, 8); 
   lv_scale_set_total_tick_count(x_scale, 9);            // match point count
   lv_scale_set_major_tick_every(x_scale, 1);            // every tick is major
   lv_scale_set_label_show(x_scale, true);
   lv_obj_set_style_pad_hor(x_scale, lv_chart_get_first_point_center_offset(chart), 0);
   lv_obj_align_to(x_scale, chart, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
   lv_obj_set_style_bg_opa(x_scale, LV_OPA_100, LV_PART_MAIN);

   // add switch to toggle between sinewave source and microphone data
   chart_sw = lv_switch_create(parent);
   lv_obj_add_event_cb(chart_sw, switch_event_handler, LV_EVENT_ALL, NULL);
   lv_obj_remove_state(chart_sw, LV_STATE_CHECKED);
   lv_obj_align_to(chart_sw, x_scale, LV_ALIGN_OUT_BOTTOM_MID, 0, 9);      

   lv_obj_t *sw_label1 = lv_label_create(parent);
   lv_label_set_text(sw_label1, "Simulation");
   lv_obj_add_style(sw_label1, &style_label_default, LV_PART_MAIN);
   lv_obj_align_to(sw_label1, chart_sw, LV_ALIGN_OUT_LEFT_MID, -10, 0);   

   lv_obj_t *sw_label2 = lv_label_create(parent);
   lv_label_set_text(sw_label2, "Microphone");
   lv_obj_add_style(sw_label2, &style_label_default, LV_PART_MAIN);   
   lv_obj_align_to(sw_label2, chart_sw, LV_ALIGN_OUT_RIGHT_MID, 5, 0);     

   /**
    * Prepare buffers for sinewave simulation
    */
#define N_SAMPLES 1024    // Must be a power of 2, e.g., 512, 1024, 2048, 4096
   // int16_t *mic_bufr = (int32_t *)heap_caps_malloc(N_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM); 
   disp_bufr = (lv_coord_t *)heap_caps_malloc(N_SAMPLES * sizeof(lv_coord_t), MALLOC_CAP_SPIRAM);   
   fft_bufr = (lv_coord_t *)heap_caps_malloc(N_SAMPLES * sizeof(lv_coord_t), MALLOC_CAP_SPIRAM); 

   // get info from fft to set up buffers
   fft_table_t *fft_table = fft.init(N_SAMPLES, N_SAMPLES, SPECTRAL_SLIDING);

   /**
    * @brief Create in-place timer to send data to the chart
    */
   chart_timer = lv_timer_create([](lv_timer_t *timer) {
      static float *working_buffer = (float *)heap_caps_malloc((N_SAMPLES * sizeof(float)), MALLOC_CAP_SPIRAM); 
      static float *input = (float *)heap_caps_malloc((N_SAMPLES * sizeof(float)), MALLOC_CAP_SPIRAM);  
      static int16_t *mic_bufr = (int16_t *)heap_caps_malloc(N_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);          
      uint16_t i, j;  
      static float sine_freq = 500; // 1kHz tone  
      float phase = 0.0f;   
      rec_command_t rec_command; 
      rec_status_t rec_status_cb;

      if(lv_tileview_get_tile_act(tileview) == tv_demo_page3) { 
         // if chart sw is not checked, display sinewave data 
         if(!lv_obj_has_state(chart_sw, LV_STATE_CHECKED)) {
            // Create sinewave in bufr
            phase = 0.0f;
            const float PHASE_STEP = TWO_PI * sine_freq / AUDIO_SAMPLE_RATE;   // sine freq defined here
            for (int j = 0; j < N_SAMPLES; j++) {
               input[j] = sinf(phase) * 0.333;
               phase += PHASE_STEP;
            }
            // Perform FFT on the sinewave data
            fft.compute(input, working_buffer);

            for(i=0; i<N_SAMPLES; i++) {
               fft_bufr[i] = int(working_buffer[i]);
            }
            lv_chart_set_point_count(chart, N_SAMPLES/2);
            lv_chart_set_ext_y_array(chart, ser2, (lv_coord_t *)fft_bufr);     
            
            sine_freq += 5;
            if(sine_freq > 3000)
               sine_freq = 500;

         // Start audio recording with a single frame.
         } else { 
            rec_command.mode = REC_MODE_START;
            rec_command.data_dest = mic_bufr;   // audio data goes into here
            rec_command.filepath = NULL;
            rec_command.duration_secs = 0.0;    // duration = 0 will record 1 frame
            rec_command.samples_per_frame = N_SAMPLES;  // num 16 bit samples per frame
            rec_command.use_lowpass_filter = false;
            xQueueSend( h_QueueAudioRecCommand, ( void * ) &rec_command, ( TickType_t ) 1000 );

            // Wait for the recording to complete
            while(true) {
               if(xQueueReceive( h_QueueAudioRecStatus, ( void * ) &rec_status_cb, 
                     ( TickType_t ) 1000 ) == pdTRUE) {
                  break;
               }
            }

            // Convert integer mic data to float for fft
            for(i=0; i<N_SAMPLES; i++) {
               input[i] = float(mic_bufr[i]);
            }

            // Perform FFT on the sinewave data
            fft.compute(input, working_buffer);

            // Convert fft output data to lv_coord data for chart display
            for(i=0; i<N_SAMPLES; i++) {
               fft_bufr[i] = int(working_buffer[i] / 1000);
            }            
            lv_chart_set_point_count(chart, N_SAMPLES/2);
            lv_chart_set_ext_y_array(chart, ser2, (lv_coord_t *)fft_bufr);  
         }
      }
      vTaskDelay(2);
   }, 50, NULL); 

}


/********************************************************************
 * Event handler for tone freq slider widget
 */
static void slider_event_cb(lv_event_t * e)
{
   lv_obj_t * slider = lv_event_get_target_obj(e);
   lv_event_code_t code = lv_event_get_code(e);
   char buf[20];
   float freq;
   int16_t volume;
   uint32_t stop_start_delay = 100;

   if(slider == tone_slider && code == LV_EVENT_VALUE_CHANGED) {
      freq = float(lv_slider_get_value(slider));
      lv_snprintf(buf, sizeof(buf), "Freq= %d Hz", (int)freq);
      lv_label_set_text(tone_slider_label, buf);
      if(db1_latched) {       // if tone button latched...
         audio.stopTone();
         vTaskDelay(stop_start_delay);
         audio.playTone(freq, AUDIO_SAMPLE_RATE, tone_volume * 20, 0.0);
      }
   }
   else if(slider == tone_vol_slider && code == LV_EVENT_VALUE_CHANGED) {
      freq = float(lv_slider_get_value(tone_slider));
      tone_volume = int(lv_slider_get_value(slider));
      lv_snprintf(buf, sizeof(buf), "Volume= %d%%", tone_volume);
      lv_label_set_text(tone_vol_slider_label, buf);
      if(db1_latched) {       // if tone button latched...
         audio.stopTone();
         vTaskDelay(stop_start_delay);
         audio.playTone(freq, AUDIO_SAMPLE_RATE, tone_volume * 20, 0.0);
      }      
   }
}


/********************************************************************
 * Event handler for switch widget
 */
void switch_event_handler(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * obj = lv_event_get_target_obj(e);
   if(code == LV_EVENT_VALUE_CHANGED) {
      Serial.printf("State: %s\n", lv_obj_has_state(obj, LV_STATE_CHECKED) ? "On" : "Off");
   }
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   uint16_t i;

   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(butn == demo_butn1 && code == LV_EVENT_CLICKED) {
      db1_latched = !db1_latched; // Toggle state

      if(db1_latched) {
         lv_obj_add_state(butn, LV_STATE_CHECKED);   // Visually "pressed"
         lv_label_set_text(lv_obj_get_child(butn, 0), "Tone OFF");
         float freq = float(lv_slider_get_value(tone_slider));
         audio.playTone(freq, AUDIO_SAMPLE_RATE, tone_volume * 20, 0.0);
      } else {
         lv_obj_clear_state(butn, LV_STATE_CHECKED); // Back to normal
         lv_label_set_text(lv_obj_get_child(butn, 0), "Tone ON");
         audio.stopTone();
      }
   }
   else if(butn == next_butn) {
      lv_obj_set_tile(tileview, tv_demo_page2, LV_ANIM_ON);   // default starting page
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

