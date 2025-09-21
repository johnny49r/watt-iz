#include "main.h"


// WS2812 RGB Led (NEOPIXEL)
#define pixelCount         1
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(pixelCount, PIN_NEO_PIXEL);

#define colorSaturation 16

// Basic NEOPIXEL colors
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor orange(colorSaturation, int(float(colorSaturation * 0.7)), int(float(colorSaturation * 0.4)));
RgbColor gray(4, 4, 4);

// Audio 
// AUDIO audio;

// Utility functions
SYS_UTILS sys_utils;

/********************************************************************
 * setup()
 */
void setup(void) 
{
   char strtmp[30];
   sd_card_info_t sdcard_info;

   /**
    * GPIO pin assignments
    */
   // digitalWrite(PIN_SDCARD_PWR, LOW);    
   // pinMode(PIN_SDCARD_PWR, OUTPUT);
   // digitalWrite(PIN_SDCARD_PWR, LOW);    // Power ON  
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin   

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(250);
   Serial.println("");

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, gray);   
   strip.Show();                          // leds off

   /**
    * Initialize audio microphone & speaker drivers
    */
   if(!audio.init(AUDIO_SAMPLE_RATE)) {
      Serial.println(F("ERROR: Failed to init Audio!"));
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
    * Initialize SD Card
    */
      /**
    * Try to initialize the SD card
    */
   bool sd_ok;;
   for(int i=0; i<3; i++) {
      sd_ok = sdcard.init();
      if(!sd_ok) 
         sdcard.unmount(25);           // try to unmount
      else   
         break;
   }
   if(sd_ok) {
      sdcard.getCardInfo(&sdcard_info);   // copy card info to local struct
      switch(sdcard_info.error_code) {
         case SD_OK:
            // Display card type 
            Serial.print("\nSD Card Type: ");
            // determine card type
            switch(sdcard_info.card_type) {
               case CARD_MMC:
                  Serial.println("MMC");
                  break;

               case CARD_SD:
                  Serial.println("SDSC");
                  break;

               case CARD_SDHC:
                  Serial.println("SDHC");
                  break;

               default:
                  Serial.println("UNKNOWN");
                  break;
            }
            sprintf((char *)&strtmp, "Capacity: %dMB\n", sdcard_info.card_size / 1048576);
            Serial.print(strtmp);
            sprintf((char *)&strtmp, "Available: %dMB\n", sdcard_info.bytes_avail / 1048576);
            Serial.print(strtmp);       
            sprintf((char *)&strtmp, "Used: %dMB\n\n", sdcard_info.bytes_used / 1048576);         
            Serial.print(strtmp);         
            break;

         case SD_UNINITIALIZED:
            Serial.println("ERROR: SD is not yet initialized!");
            break;

         case MOUNT_FAILED:     
            Serial.println("ERROR: SD Card failed to mount!");
            break;

         case NOT_INSERTED:         
            Serial.println("ERROR: SD Card not inserted!");
            break;

         case CARD_FULL:        
            Serial.println("ERROR: SD Card is full - no space available!");
            break;
      }
   } else {
      Serial.println("Error: SD Card Initialization Failed!");
      strip.SetPixelColor(0, red);   
      strip.Show();
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
   static float freq = 1000;
   static bool once = false;

   /**
    * Service the lvgl graphics engine at periodical intervals (typ 10ms)
    */

   if(millis() - lv_timer_ms > lv_next_interval) {
      // Service graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }

   if(millis() - gp_timer > 1000) {
      gp_timer = millis();
      if(!once) {
         once = true;
         audio.playTone(1000, AUDIO_SAMPLE_RATE, 500, 0.61); 
      }
   }

}



