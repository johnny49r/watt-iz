/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
#define DISPLAY_POINTS           512

// Global variables 
bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
volatile uint16_t msgBoxBtnTag = MBOX_BTN_NONE;

// FFT stuff
ESP32S3_FFT fft;

/* simple white noise in [-1,1] */
static inline float frand11(void) {
   return (2.0f * (float)rand() / (float)RAND_MAX) - 1.0f;
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
   // init flash storage and load system settings
   sys_utils.initNVS();                   // call this first!   
   sys_utils.initBacklight();             // init backlight PWM control 
 
   // clear GUI events
   // gui_event_local = GUI_EVENT_NONE;

   // Initialize I2C peripheral
   if(!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)) { // init I2C for capacitive touch
      Serial.println(F("ERROR: I2C Init Failed!"));
      return false;
   }
   Wire.setClock(400000);                 // 400 khz I2C clk rate

   // Init display
   gfx.init();
   gfx.setRotation(3);
   gfx.setColorDepth(16);
   gfx.fillScreen(TFT_BLACK);
   gfx.setSwapBytes(false); // ILI9341 usually expects RGB565 as-is; enable if colors look swapped

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

   // --- Allocate a small DMA-capable SRAM bounce buffer ---
   size_t sram_pixels = SCREEN_WIDTH * SRAM_LINES;
   size_t sram_bytes  = sram_pixels * sizeof(uint16_t);
   sram_linebuf = (uint16_t *)heap_caps_malloc(sram_bytes,
         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   

   // Get the active screen object for this display
   lv_obj_t *scr = lv_display_get_screen_active(main_disp);
   // Set background color and opacity
   lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN); // 
   lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 100);

   // Create a new pointer input device
   lv_indev_t * indev = lv_indev_create();
   lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
   lv_indev_set_read_cb(indev, (lv_indev_read_cb_t)touch_read_cb);

   // Light up the LCD backlight
   sys_utils.setBrightness(sys_utils.SystemSettings.brightness);  

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
   tv_demo_page1 =  lv_tileview_add_tile(tileview, 0, 0, LV_DIR_NONE);
   // tv_demo_page2 =  lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
   // tv_demo_page3 =  lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);   
   // tv_demo_page4 =  lv_tileview_add_tile(tileview, 3, 0, LV_DIR_LEFT);      

   // build tile pages
   demo_page1(tv_demo_page1);             // build the top level switch page
   // demo_page2(tv_demo_page2);               
   // demo_page3(tv_demo_page3);  
   // demo_page4(tv_demo_page4);                 

   // default to top level page
   lv_disp_trig_activity(NULL);           // restart no activity timer
   lv_obj_set_tile(tileview, tv_demo_page1, LV_ANIM_ON);   // default starting page
}


/********************************************************************
 * Demo Page 1
 */
void demo_page1(lv_obj_t *parent)
{
   // Create a chart to display audio/FFT from the microphone
   fft_chart = lv_chart_create(parent);      
   lv_obj_set_size(fft_chart, 316, 150);    
   lv_chart_set_range(fft_chart, LV_CHART_AXIS_PRIMARY_Y, -512, 512);
   lv_chart_set_range(fft_chart, LV_CHART_AXIS_SECONDARY_Y, -600, 600); // FFT values   
   lv_obj_set_style_radius(fft_chart, 3, LV_PART_MAIN);
   lv_chart_set_type(fft_chart, LV_CHART_TYPE_LINE);   // Show lines and points too
   lv_chart_set_div_line_count(fft_chart, 5, 17);   
   lv_obj_set_style_pad_hor(fft_chart, 0, LV_PART_MAIN);
   lv_obj_set_style_size(fft_chart, 0, 0, LV_PART_INDICATOR);  // hides the points on the line
   lv_obj_set_style_bg_color(fft_chart, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_line_color(fft_chart, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
   lv_obj_set_style_border_color(fft_chart, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);      
   lv_obj_set_scrollbar_mode(fft_chart, LV_SCROLLBAR_MODE_OFF);     // don't show scrollbars on non-scrolling pages
   lv_obj_clear_flag(fft_chart, LV_OBJ_FLAG_SCROLLABLE);
   lv_obj_align(fft_chart, LV_ALIGN_TOP_MID, 0, 0);   

   ser1 = lv_chart_add_series(fft_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);  
   ser2 = lv_chart_add_series(fft_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_SECONDARY_Y);   

   // X axis as a scale aligned to the bottom 
   // static const char * tick_labels[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", ""};
   static const char * tick_labels[] = {"0", "0.5", "1", "1.5", "2", "2.5", "3", "3.5", 
            "4", "4.5", "5", "5.5", "6", "6.5", "7", "7.5", "8", ""};   
   lv_obj_t *x_scale = lv_scale_create(parent);
   lv_scale_set_mode(x_scale, LV_SCALE_MODE_HORIZONTAL_TOP);
   lv_obj_set_size(x_scale, 310, 24);
   lv_scale_set_range(x_scale, 0, 16); //8); 
   lv_scale_set_total_tick_count(x_scale, 17); //9);            // match point count
   lv_scale_set_major_tick_every(x_scale, 1);            // every tick is major
   lv_scale_set_text_src(x_scale, tick_labels);   
   lv_scale_set_label_show(x_scale, true);
   lv_obj_set_style_bg_color(x_scale, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_color(x_scale, lv_color_white(), LV_PART_MAIN);   
   lv_obj_set_style_text_font(x_scale, &lv_font_montserrat_12, LV_PART_MAIN);   
   lv_obj_set_style_line_color(x_scale, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_INDICATOR); // ticks
   lv_obj_set_style_line_color(x_scale, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
   lv_obj_set_style_pad_hor(x_scale, lv_chart_get_first_point_center_offset(fft_chart), 0);
   lv_obj_set_style_bg_opa(x_scale, LV_OPA_100, LV_PART_MAIN);
   lv_obj_align_to(x_scale, fft_chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 0); //-8);   

   lv_obj_t *x_scale_label = lv_label_create(parent);
   lv_label_set_text(x_scale_label, "Freq in Khz");
   lv_obj_add_style(x_scale_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(x_scale_label, &lv_font_montserrat_16, LV_PART_MAIN);   
   lv_obj_align_to(x_scale_label, x_scale, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);  

   lv_obj_t *y_scale = lv_scale_create(parent);
   lv_scale_set_mode(y_scale, LV_SCALE_MODE_VERTICAL_RIGHT);
   lv_obj_set_size(y_scale, 24, 132);
   lv_scale_set_range(y_scale, -6, 6);  
   lv_scale_set_total_tick_count(y_scale, 5); //9);            // match point count
   lv_scale_set_major_tick_every(y_scale, 1);            // every tick is major
   // lv_scale_set_text_src(y_scale, tick_labels);   
   lv_scale_set_label_show(y_scale, true);
   lv_obj_set_style_bg_color(y_scale, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_text_color(y_scale, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_MAIN);   
   lv_obj_set_style_text_font(y_scale, &lv_font_montserrat_12, LV_PART_MAIN);   
   lv_obj_set_style_line_color(y_scale, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_INDICATOR); // ticks
   lv_obj_set_style_line_color(y_scale, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_MAIN);
   lv_obj_set_style_pad_hor(y_scale, lv_chart_get_first_point_center_offset(fft_chart), 0);
   lv_obj_set_style_bg_opa(y_scale, LV_OPA_100, LV_PART_MAIN);
   lv_obj_align_to(y_scale, fft_chart, LV_ALIGN_OUT_RIGHT_MID, -22, 1); //-8);     

   lv_obj_t *y_scale_label = lv_label_create(parent);
   lv_label_set_text(y_scale_label, "dB");
   lv_obj_add_style(y_scale_label, &style_label_default, LV_PART_MAIN);
   lv_obj_set_style_text_font(y_scale_label, &lv_font_montserrat_16, LV_PART_MAIN);   
   lv_obj_set_style_text_color(y_scale_label, lv_palette_lighten(LV_PALETTE_GREEN, 1), LV_PART_MAIN);     
   lv_obj_align(y_scale_label, LV_ALIGN_TOP_RIGHT, -32, 1);  

   // Start scan & display
   start_scan_butn = lv_btn_create(parent);
   lv_obj_set_size(start_scan_butn, 100, 32);
   lv_obj_add_style(start_scan_butn, &style_butn_released, LV_PART_MAIN | LV_STATE_DEFAULT);
   lv_obj_add_style(start_scan_butn, &style_butn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);   
   lv_obj_add_event_cb(start_scan_butn, &butn_event_cb, LV_EVENT_CLICKED, NULL);   
   lv_obj_set_ext_click_area(start_scan_butn, 12);
   lv_obj_align(start_scan_butn, LV_ALIGN_BOTTOM_RIGHT, 0, -5);  

   lv_obj_t *start_label1 = lv_label_create(start_scan_butn);
   lv_obj_add_style(start_label1, &style_label_default, LV_PART_MAIN);
   lv_label_set_text(start_label1, "FFT Scan"); 
   lv_obj_set_style_text_align(start_label1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);     
   lv_obj_center(start_label1); 

   // Cutoff frequency dropdown
   dd_freq = lv_dropdown_create(parent);
   lv_obj_set_size(dd_freq, 90, 26);
   lv_dropdown_set_options(dd_freq, "0 KHz\n"
         "0.5 KHz\n1.0 KHz\n1.5 KHz\n2.0 KHz\n2.5 KHz\n3.0 KHz\n3.5 KHz\n4.0 KHz\n"
         "4.5 KHz\n5.0 KHz\n5.5 KHz\n6.0 KHz\n6.5 KHz\n7.0 KHz\n7.5 KHz\n8.0 KHz");
                                                                                                                                                                                                                                                                                                             
   lv_dropdown_set_selected(dd_freq, 6);
   lv_obj_add_event_cb(dd_freq, dd_event_handler_cb, LV_EVENT_ALL, NULL);    
   lv_obj_align(dd_freq, LV_ALIGN_BOTTOM_LEFT, 4, -5);   

   // Q factor dropdown
   dd_qfactor = lv_dropdown_create(parent);
   lv_obj_set_size(dd_qfactor, 90, 26);
   lv_dropdown_set_options(dd_qfactor, "Q-0.5\n"
         "Q-0.6\nQ-0.7\nQ-0.8\nQ-0.9\nQ-1.0"); 
                                                                                                                                                                                                                                                                                                             
   lv_dropdown_set_selected(dd_qfactor, 0);
   lv_obj_add_event_cb(dd_qfactor, dd_event_handler_cb, LV_EVENT_ALL, NULL);    
   lv_obj_align(dd_qfactor, LV_ALIGN_BOTTOM_MID, 0, -5);   

}


/********************************************************************
*  @fn Dropdown menu event handler. 
*/
void dd_event_handler_cb(lv_event_t * e)
{
   char buf[20];
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);

}


/********************************************************************
 * @brief Get the cutoff frequency from the dropdown menu
 */
float ddGetCutoffFreq(void)
{
   float f = float(lv_dropdown_get_selected(dd_freq)) * 500;
   return f;
}


/********************************************************************
 * @brief Return the float QFactor derived from the dropdown menu.
 */
float ddGetQFactor(void)
{
   float f = 0.5 + (float(lv_dropdown_get_selected(dd_qfactor)) * 0.1);
   return f;
}


/********************************************************************
 * @brief Start generating sinewaves from 100 to 8000hz and display
 * fft results on fft_chart.
 */
void startFFTScan(float cutoff_freq, float qfactor)
{
#define FS              16000
#define NUM_AVG         16                // blocks to average
#define SCAN_FFT_SIZE   1024
#define QFACTOR         0.5               // 0.5 -> 1.0

   static float x[SCAN_FFT_SIZE], y[SCAN_FFT_SIZE];              // white noise - time domain
   static float X[2*SCAN_FFT_SIZE], Y[2*SCAN_FFT_SIZE];          // FFT buffers
   static float Px[SCAN_FFT_SIZE/2+1], Py[SCAN_FFT_SIZE/2+1];    // accumulate power (0..Nyquist) 
   static lv_coord_t plot_pts[SCAN_FFT_SIZE/2+1];

   ESP32S3_LP_FILTER lp_filter;

   // Initialize the low pass filter
   // cutoff_freq = frequency in Hz
   // *** Last parameter 'Qfactor' == 0.5 <smoother cutoff rate>, 1.0 <sharper cutoff rate>
   lp_filter.init(cutoff_freq, AUDIO_SAMPLE_RATE, qfactor);

   // Initialize the fft engine
   ESP32S3_FFT afft;                      // FFT object
   fft_table_t *fft_table = afft.init(SCAN_FFT_SIZE, SCAN_FFT_SIZE, SPECTRAL_AVERAGE); 

   // clear accumulators
   for (int c=0; c<=SCAN_FFT_SIZE/2; c++) { 
      Px[c]=0; 
      Py[c]=0; 
   }

   for (int blk=0; blk<NUM_AVG; blk++) {     // take averaged samples

      // Generate white noise block
      for (int wn=0; wn<SCAN_FFT_SIZE; wn++) {
         x[wn] = frand11();
      }

      // optional warm-up (recommended at least once after coeff/state reset)
      // (or just discard first part of y)
      // dsps_biquad_f32_aes3(x, y, N, coeffs, w);

      // Apply filter to x data. x is unfiltered, y is filtered
      lp_filter.apply(x, y, SCAN_FFT_SIZE);     

      // FFT X
      afft.compute(x, X, true);           // compute fft for X (unfiltered data)

      // FFT Y
      afft.compute(y, Y, true);           // compute fft for Y (filtered data)

      // Accumulate power (0..N/2)
      for (int j=0; j<=SCAN_FFT_SIZE/2; j++) {
         Px[j] += X[j];
         Py[j] += Y[j];
      }
   }

   // compute magnitude response in dB (store/print/plot as you like)
   for (int k=1; k<=SCAN_FFT_SIZE/2; k++) { // skip DC if you want
      float Hmag = sqrtf(Py[k] / (Px[k]) + 1e-20f);
      float HdB  = 20.0f * log10f(Hmag + 1e-20f);
      float f_hz = (float)k * (float)AUDIO_SAMPLE_RATE / (float)SCAN_FFT_SIZE;
      plot_pts[k] = int(HdB * 100);
      // Serial.printf("f=%.2f, Hmag=%.2f, Hdb=%.2f\n", f_hz, Hmag, HdB);
      // Serial.printf("Px=%.3f, Py=%.3f\n", Px[k], Py[k]);
      // log f_hz, HdB
   }
   // Show the filter response plot
   lv_chart_set_point_count(fft_chart, SCAN_FFT_SIZE/2);
   lv_chart_set_ext_y_array(fft_chart, ser1, plot_pts);  

   // Free FFT memory
   afft.end();
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

   // if(slider == tone_freq_slider && code == LV_EVENT_VALUE_CHANGED) {
   //    freq = float(lv_slider_get_value(slider));
   //    lv_snprintf(buf, sizeof(buf), "Freq= %d Hz", (int)freq);
   //    lv_label_set_text(tone_freq_slider_label, buf);
   //    if(db1_latched) {       // if tone button latched...
   //       audio.stopTone();    // stop tone and wait till finished
   //       tone_volume = int(lv_slider_get_value(tone_vol_slider));
   //       // Restart tone gen with new freq
   //       audio.playTone(freq, RING_MODE_STEADY, tone_volume, 0.0, true);
   //    }
   // }
  
}


/********************************************************************
 * Event handler for switch widget
 */
void switch_event_handler(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t * obj = lv_event_get_target_obj(e);
   if(code == LV_EVENT_VALUE_CHANGED) {
      // Serial.printf("State: %s\n", lv_obj_has_state(obj, LV_STATE_CHECKED) ? "On" : "Off");
   }
}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   uint16_t i;
   int16_t vol = 0;
   float freq, qfactor;
   capture_status_t cap_stat;
   bool stok;

   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

   if(butn == start_scan_butn && code == LV_EVENT_CLICKED) {
      Serial.println("start scan butn");
      freq = ddGetCutoffFreq();
         Serial.printf("freq=%.2f\n", freq);
      qfactor = ddGetQFactor();
         Serial.printf("qfactor=%.2f\n", qfactor);
      startFFTScan(freq, qfactor);
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
