/**
 * main.cpp
 */
#include "main.h"

// WS2812 RGB Led (NEOPIXEL)
#define pixelCount         1
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(pixelCount, PIN_NEO_PIXEL);

#define colorSaturation 16    // NEOPIXEL brightness 0 - 255

// Basic NEOPIXEL colors
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor orange(colorSaturation, int(float(colorSaturation * 0.7)), int(float(colorSaturation * 0.4)));
RgbColor gray(4, 4, 4);



/********************************************************************
 * setup()
 */
void setup(void) 
{
   char strtmp[30];

   /**
    * GPIO pin assignments
    */
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);   // set as input w/pullup resistor   
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);      // resistive touch IRQ pin

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(1000);
   Serial.println("");

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, gray);   
   strip.Show();                          // leds off

   /**
    * Initialize graphics, backlight dimming, touch screen, and demo widgets
    */                    
   if(!guiInit()) {
      strip.SetPixelColor(0, red);   
      strip.Show();        
      Serial.println("ERROR: Failed to Init GUI");
   } else {
      setDefaultStyles();                 // init lvgl styles
      demoBuilder();                      // prebuild demo pages
   }

   /**
    * Initialize the ADC resolution and input range
    */
   sys_utils.initADC(12, ADC_11db);
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t gp_timer = millis();
   float batv, ichg;
   uint8_t soc;
   system_power_t * sys_pwr;

   /**
    * Service the lvgl graphics engine at periodical intervals (typical = 10ms)
    */
   if(millis() - lv_timer_ms > lv_next_interval) {
      // Refresh graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }

   if(millis() - gp_timer > 1000) {
      gp_timer = millis();
      sys_pwr = sys_utils.getPowerInfo();
      // batv = sys_utils.getBatteryVolts();
      // ichg = sys_utils.getBatChgCurrent();
      // extv = sys_utils.getExtPowerVolts();
      soc = sys_utils.calcBatSOC(batv);
      Serial.printf("batv=%.2f, ichg=%.2f, SOC=%d, time to chrg (mins)=%d\n", sys_pwr->battery_volts, 
            sys_pwr->charge_current, sys_pwr->state_of_charge, sys_pwr->time_to_charge);
   }

}



