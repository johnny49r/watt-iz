#include "main.h"

// WS2812 RGB Led
#define pixelCount         1
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(pixelCount, PIN_NEO_PIXEL);

#define colorSaturation 16

// Basic colors
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor orange(colorSaturation, int(float(colorSaturation * 0.7)), int(float(colorSaturation * 0.4)));


/********************************************************************
 * setup()
 */
void setup(void) 
{
   Serial.begin(115200);                  // init serial monitor 
         vTaskDelay(1000);
   /**
    * GPIO pin assignments
    */
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);   // set as input w/pullup resistor
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, green);   
   strip.Show();                          // leds off

   /**
    * Initialize graphics, backlight dimming, touch screen, and demo widgets
    */                    
          Serial.println("\nhere 0");
   guiInit();

   /**
    * Set LVGL style objects. Must be done before creating graphics.
    */
   setDefaultStyles();

   /**
    * Build tileview pages and demo graphics
    */
   pageBuilder();        
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();

   /**
    * Service the lvgl graphics engine at periodical intervals (typ 10ms)
    */
   if(millis() - lv_timer_ms > lv_next_interval) {
      // Service graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }
}


