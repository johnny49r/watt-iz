/********************************************************************
 * @brief gui.cpp
 */

#include "gui.h"

// global variables
static int32_t * mic_bufr;
static lv_coord_t * disp_bufr;  
lv_coord_t * fft_bufr;
#define DISPLAY_POINTS           512

bool NVS_OK = false;                   // global OK flag for non-volatile storage lib
uint16_t msgBoxBtnTag = MBOX_BTN_NONE;

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
 * Callback to read touch screen coords
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
   LV_IMAGE_DECLARE(power_measurement_icon_300x150_bw);

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
   lv_image_set_src(img1, &power_measurement_icon_300x150_bw);
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
 * Demo Page 1
 */
void demo_page2(lv_obj_t *parent)
{
#define N_SAMPLES    600                  // chart x axis == 10 minutes, updated every 1 sec 
   char buf[40]; 
   int16_t i = 0;

   // Y axis scale major tick custom text 
   static const char * yvbat_scale_text[] = {"0.0", "1.0", "2.0", "3.0", "4.0", "5.0", "6.0", NULL};
   static const char * yichg_scale_text[] = {"0", "200", "400", "600", "800", "1000", "1200", NULL};

   // Create a chart to display audio/FFT from the microphone
   pwr_chart = lv_chart_create(parent);      
   lv_obj_set_size(pwr_chart, 320, 160); //316, 120);    
   lv_chart_set_point_count(pwr_chart, N_SAMPLES);
   lv_chart_set_range(pwr_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 6000);  // 0 - 6V (in mv)
   lv_chart_set_range(pwr_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 1200);   // 0 - 1.2A (in ma)
   lv_obj_set_style_radius(pwr_chart, 3, LV_PART_MAIN);
   lv_obj_set_style_bg_color(pwr_chart, lv_color_black(), LV_PART_MAIN);
   lv_obj_set_style_line_color(pwr_chart, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
   lv_obj_set_style_border_color(pwr_chart, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);   
   lv_chart_set_div_line_count(pwr_chart, 7, 11);
   lv_chart_set_type(pwr_chart, LV_CHART_TYPE_LINE);   // Show lines and points too
   lv_chart_set_update_mode(pwr_chart, LV_CHART_UPDATE_MODE_SHIFT);
   lv_obj_set_scrollbar_mode(pwr_chart, LV_SCROLLBAR_MODE_OFF);     // don't show scrollbars on non-scrolling pages
   lv_obj_clear_flag(pwr_chart, LV_OBJ_FLAG_SCROLLABLE);
   lv_obj_set_style_size(pwr_chart, 0, 0, LV_PART_INDICATOR);
   lv_obj_align(pwr_chart, LV_ALIGN_TOP_MID, 0, 5);   

   ser1 = lv_chart_add_series(pwr_chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);  
   ser2 = lv_chart_add_series(pwr_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_SECONDARY_Y);   

   // X axis as a scale aligned to the bottom 
   lv_obj_t *xtime_scale = lv_scale_create(pwr_chart);
   lv_scale_set_mode(xtime_scale, LV_SCALE_MODE_HORIZONTAL_TOP);
   lv_obj_set_size(xtime_scale, 318, 22);
   lv_scale_set_range(xtime_scale, 0, 60); 
   lv_scale_set_total_tick_count(xtime_scale, 11);            // match point count
   lv_scale_set_major_tick_every(xtime_scale, 1);            // every tick is major
   lv_scale_set_label_show(xtime_scale, false);
   lv_obj_set_style_pad_hor(xtime_scale, lv_chart_get_first_point_center_offset(pwr_chart), 0); 
   lv_obj_set_style_line_color(xtime_scale, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);   
   lv_obj_set_style_line_color(xtime_scale, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_INDICATOR);     
   lv_obj_set_style_line_color(xtime_scale, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_ITEMS);    
   lv_obj_align(xtime_scale, LV_ALIGN_BOTTOM_MID, 0, 10);      

   // Y axis battery voltage scale
   lv_obj_t *yvbat_scale = lv_scale_create(pwr_chart);
   lv_scale_set_mode(yvbat_scale, LV_SCALE_MODE_VERTICAL_RIGHT);
   lv_obj_set_size(yvbat_scale, 22, 140);
   lv_scale_set_range(yvbat_scale, 0, 12); 
   lv_scale_set_total_tick_count(yvbat_scale, 7);            // match point count
   lv_scale_set_major_tick_every(yvbat_scale, 1);        
   lv_scale_set_text_src(yvbat_scale, yvbat_scale_text);
   lv_scale_set_label_show(yvbat_scale, true);
   lv_obj_set_style_pad_hor(yvbat_scale, lv_chart_get_first_point_center_offset(pwr_chart), 0);
   lv_obj_set_style_line_color(yvbat_scale, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);   
   lv_obj_set_style_line_color(yvbat_scale, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);     
   lv_obj_set_style_line_color(yvbat_scale, lv_palette_main(LV_PALETTE_BLUE), LV_PART_ITEMS);    
   lv_obj_set_style_text_color(yvbat_scale, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);    
   lv_obj_set_style_text_color(yvbat_scale, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_ITEMS);     
   lv_obj_align(yvbat_scale, LV_ALIGN_LEFT_MID, -16, 0);  

   // Y axis battery charge current scale
   lv_obj_t *yichg_scale = lv_scale_create(pwr_chart);
   lv_scale_set_mode(yichg_scale, LV_SCALE_MODE_VERTICAL_LEFT);
   lv_obj_set_size(yichg_scale, 22, 140);
   lv_scale_set_range(yichg_scale, 0, 12); 
   lv_scale_set_total_tick_count(yichg_scale, 7);            // match point count
   lv_scale_set_major_tick_every(yichg_scale, 1);        
   lv_scale_set_text_src(yichg_scale, yichg_scale_text);
   lv_scale_set_label_show(yichg_scale, true);
   lv_obj_set_style_pad_hor(yichg_scale, lv_chart_get_first_point_center_offset(pwr_chart), 0);
   lv_obj_set_style_line_color(yichg_scale, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);   
   lv_obj_set_style_line_color(yichg_scale, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);     
   lv_obj_set_style_line_color(yichg_scale, lv_palette_main(LV_PALETTE_RED), LV_PART_ITEMS);    
   lv_obj_set_style_text_color(yichg_scale, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);    
   lv_obj_set_style_text_color(yichg_scale, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_ITEMS);     
   lv_obj_align(yichg_scale, LV_ALIGN_RIGHT_MID, 16, 0);    
   
   batv_label = lv_label_create(parent);   
   lv_obj_set_size(batv_label, 180, 22);
   lv_obj_set_style_pad_ver(batv_label, 1, LV_PART_MAIN);
   lv_label_set_text(batv_label, "Battery: 0.00 Volts");
   lv_obj_set_style_text_color(batv_label, lv_color_white(), LV_PART_MAIN);
   lv_obj_align_to(batv_label, pwr_chart, LV_ALIGN_OUT_BOTTOM_LEFT, 5, 4);    

   ichg_label = lv_label_create(parent);   
   lv_obj_set_size(ichg_label, 210, 22);
   lv_obj_set_style_pad_ver(ichg_label, 1, LV_PART_MAIN);
   lv_label_set_text(ichg_label, "Chg Current: 000 mA");
   lv_obj_set_style_text_color(ichg_label, lv_color_white(), LV_PART_MAIN);   
   lv_obj_align_to(ichg_label, batv_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);    

   soc_label = lv_label_create(parent);   
   lv_obj_set_size(soc_label, 160, 22);
   lv_obj_set_style_pad_ver(soc_label, 1, LV_PART_MAIN);   
   lv_label_set_text(soc_label, "SOC: 000%");
   lv_obj_set_style_text_color(soc_label, lv_color_white(), LV_PART_MAIN);   
   lv_obj_align_to(soc_label, ichg_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);      

   chg_led = lv_led_create(parent);
   lv_obj_align(chg_led, LV_ALIGN_CENTER, 0, 0);
   lv_led_set_brightness(chg_led, 250);
   lv_led_on(chg_led);
   lv_led_set_color(chg_led, lv_palette_main(LV_PALETTE_GREEN));
   lv_obj_align_to(chg_led, ichg_label, LV_ALIGN_OUT_RIGHT_MID, 60, -4); 

   // Hide all series points to start with
   lv_chart_set_all_values(pwr_chart, ser1, LV_CHART_POINT_NONE);
   lv_chart_set_all_values(pwr_chart, ser2, LV_CHART_POINT_NONE);   

   /**
    * @brief Create lambda (in-place) timer to send data to the chart
    */
   chart_timer = lv_timer_create([](lv_timer_t *timer) {
      char buf[40];  
      system_power_t *pwr_info = sys_utils.getPowerInfo();

      sprintf(buf, "Battery Volts: %.2f", pwr_info->battery_volts);
      lv_label_set_text(batv_label, buf);

      sprintf(buf, "Charge Current: %.0f mA", pwr_info->charge_current);
      lv_label_set_text(ichg_label, buf);    
      
      sprintf(buf, "SOC: %d%%", pwr_info->state_of_charge);
      lv_label_set_text(soc_label, buf);      
      
      // Color charge LED red if charging, green if not.
      if(pwr_info->battery_volts < 4.08 || pwr_info->charge_current > 140) {
         lv_led_set_color(chg_led, lv_palette_main(LV_PALETTE_RED));
      } 
      else if(pwr_info->battery_volts > 4.10 && pwr_info->charge_current < 140) {
         lv_led_set_color(chg_led, lv_palette_main(LV_PALETTE_GREEN));
      } 
      // Add data points to the chart 
      lv_chart_set_next_value(pwr_chart, ser1, int(pwr_info->battery_volts * 1000));
      lv_chart_set_next_value(pwr_chart, ser2, int(pwr_info->charge_current));         
   }, 1000, NULL); 

}


/********************************************************************
*  @fn Butn event handler. Comes here on button click event.
*/
void butn_event_cb(lv_event_t * e)
{
   lv_event_code_t code = lv_event_get_code(e);
   lv_obj_t *butn = (lv_obj_t *)lv_event_get_target(e);

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