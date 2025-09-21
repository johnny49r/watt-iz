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


/********************************************************************
 * setup()
 */
void setup(void) 
{
   char ddlist[100];                      // cString for file info dropdown list
   char strtmp[30];
   String filelist;
   String substr;
   int16_t nlbeg;
   int16_t nlend = 0;

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(250);
   Serial.println("");
   /**
    * GPIO pin assignments
    */
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);   // set as input w/pullup resistor
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, gray);   
   strip.Show();                          // leds off

   /**
    * Initialize graphics, backlight dimming, touch screen, and demo widgets
    */                    
   guiInit();

   /**
    * Set LVGL style objects. Must be done before creating graphics.
    */
   setDefaultStyles();

   /**
    * Build tileview pages and demo graphics
    */
   demoBuilder();        

   /**
    * Initialize SD Card
    */
   sdcard.init();

   switch(sdcard.sd_card_info.error_code) {
      case SD_OK:
         // Display card type 
         Serial.print("SD Card Type: ");
         // determine card type
         switch(sdcard.sd_card_info.card_type) {
            case CARD_MMC:
               Serial.println("MMC");
               strcpy((char *)&ddlist, "Card Type: MMC");
               break;

            case CARD_SD:
               Serial.println("SDSC");
               strcpy((char *)&ddlist, "Card Type: SDSC");
               break;

            case CARD_SDHC:
               strcpy((char *)&ddlist, "Card Type: SDHC");
               Serial.println("SDHC");
               break;

            default:
               strcpy((char *)&ddlist, "Card Type: Unknown");
               Serial.println("UNKNOWN");
               break;
         }
         sprintf((char *)&strtmp, "\nCapacity: %d MB", sdcard.sd_card_info.card_size / 1048576);
         Serial.print(strtmp);
         strcat(ddlist, strtmp);
         sprintf((char *)&strtmp, "\nAvailable: %d MB", sdcard.sd_card_info.bytes_avail / 1048576);
         Serial.print(strtmp);
         strcat(ddlist, strtmp);         
         sprintf((char *)&strtmp, "\nUsed: %.2f MB\n", float(sdcard.sd_card_info.bytes_used) / 1048576.0F);         
         Serial.print(strtmp); 
         strcat(ddlist, strtmp);           

         filelist.clear();
         sdcard.listDir("/", filelist, 5);
               Serial.println(filelist);
         nlbeg = 0;
         while(true) {
            nlend = filelist.indexOf("\n", nlbeg);  // find next newline
            if(nlend == -1)
               break;
            substr = filelist.substring(nlbeg, nlend);
                     
            if(substr.length() < 1) break;      // exit if no more text

            if(substr.startsWith("[")) {
               substr = filelist.substring(nlbeg +1, nlend-1); // strip [] brackets around path name
               fileListAddDir(substr);
            }
            else if(!substr.startsWith("<DIR>")) {     // ignore redundant <DIR> line
               substr = filelist.substring(nlbeg, nlend); 
               fileListAddFile(substr);
            }
            nlbeg = ++nlend;                
         }
         break;

      case SD_UNINITIALIZED:
         strcpy(ddlist, "Card Type: Not Initialized!\n"
                  "Capacity: ?\n"
                  "Available: ?\n"
                  "Used: ?");
         Serial.println("ERROR: SD is not yet initialized!");
         break;

      case MOUNT_FAILED:
         strcpy(ddlist, "Card Type: Failed to mount!\n"
                  "Capacity: ?\n"
                  "Available: ?\n"
                  "Used: ?");      
         Serial.println("ERROR: SD Card failed to mount!");
         break;

      case NOT_INSERTED:
         strcpy(ddlist, "Card Type: Not Inserted!\n"
                  "Capacity: ?\n"
                  "Available: ?\n"
                  "Used: ?");          
         Serial.println("ERROR: SD Card not inserted!");
         break;

      case CARD_FULL:
         strcpy(ddlist, "Card Type: Card Full!\n"
                  "Capacity: ?\n"
                  "Available: ?\n"
                  "Used: ?");          
         Serial.println("ERROR: SD Card is full - no space available!");
         break;
   }
   updateDDList((char *)&ddlist);

}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t gp_timer = millis();
   static sd_speed_t *sd_speed;
   bool ret;

   /**
    * Service the lvgl graphics engine at periodical intervals (typ 10ms)
    */
   if(millis() - lv_timer_ms > lv_next_interval) {
      // Service graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }

   /**
    * Check for events from GUI (button clicks, etc.)
    */
   int16_t gev = getGUIevents();     
   switch(gev) {
      // Start a new recording for STT demo
      case GUI_EVENT_START_SPEED_TEST:
         updateWriteSpeed(0.0);           // set dials back to zero
         updateReadSpeed(0.0);
         lv_refr_now(NULL);               // force graphics refresh
         lv_timer_handler(); 
         sd_speed = sdcard.speed_test(SD_TEST_SEQUENTIAL);
         updateWriteSpeed(sd_speed->wr_mbs);
         updateReadSpeed(sd_speed->rd_mbs);
         break;

      case GUI_EVENT_START_FORMAT:
         Serial.println(F("Starting SD Card Format..."));         
         disableFormatButn();             // show format in progress
         lv_refr_now(NULL);               // force graphics refresh
         gp_timer = millis();
         while(millis() - gp_timer < 200) {
            lv_timer_handler();          
            vTaskDelay(10);
         }
         sdcard.formatSDCard();           // format SD w/FaT32
         enabFormatButn();                // re-enable format butn to show format complete
         break;

      case GUI_EVENT_CREATE_CCF:
         // Write a new config file. Values are derived from 'credentials.h'
         ret = sdcard.writeConfigFile(SYS_CRED_FILENAME);
         if(!ret)
            Serial.println(F("Error: Failed to write config file!"));
         break;
   }
}




