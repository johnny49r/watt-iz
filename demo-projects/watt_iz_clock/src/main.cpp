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
RgbColor gray(2, 2, 2);


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
   strip.SetPixelColor(0, blue);   
   strip.Show();                          // leds off

   /**
    * Init SD Card
    */
   if(!sdcard.init()) {
      strip.SetPixelColor(0, red);   
      strip.Show();           
      Serial.println("ERROR: Failed to Init SD Card!");
   } 

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
    * Initialize the RTC chip - DS3231
    */
   if(!sys_utils.RTCInit(PIN_I2C_SDA, PIN_I2C_SCL)) {
      Serial.println("ERROR: Failed to init RTC Chip!");
   } 

}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t gp_timer = millis();
   static uint8_t last_second = 0;

   /**
    * Service the lvgl graphics engine at periodical intervals (typical = 10ms)
    */
   if(millis() - lv_timer_ms > lv_next_interval) {
      // Refresh graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }

   RtcDateTime now = sys_utils.RTCgetDateTime(); 
   if(now.Second() != last_second) {
      last_second = now.Second();
      refreshDateTime(now);
   }
}


/********************************************************************
 * Print date/time string to console
 */
void printDateTime(const RtcDateTime& dt)
{
    char datestring[26];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
    Serial.println("");
}


