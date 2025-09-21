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

// TTS demo message
char demo_message[] = {"Hello. This is the Abbycus E-S-P 32 speech enabled development board. " 
      "Please check our github website for more information @ https://github.com/johnny49r/watt-iz"};

// Chat GPT demo question
char chat_question[] = {"what is the elevation of denver, colorado?"};

/********************************************************************
 * setup()
 */
void setup(void) 
{
   char *item_str;
   char wifi_ssid[40];
   char wifi_password[60];
   uint8_t wifi_connect_count = 0;
   uint32_t wifi_timeout = millis();
   String output;

   /**
    * GPIO pin assignments
    */
   // digitalWrite(PIN_SDCARD_PWR, HIGH);   
   digitalWrite(PIN_SDCARD_PWR, LOW);    
   pinMode(PIN_SDCARD_PWR, OUTPUT);
   digitalWrite(PIN_SDCARD_PWR, LOW);    // Power OFF   

   // pinMode(PIN_SD_CARD_DETECT, INPUT);    // set as input for now 
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(1000);
   Serial.println(SOFTWARE_VERSION);

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
      // setLabelText(demo_message);
   }

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
   if(!sd_ok) {                   
      Serial.println(F("Error: Failed to init sd card!"));
      strip.SetPixelColor(0, red);   
      strip.Show();             
   } else {
      Serial.println(F("SD Card Init OK!"));

      sdcard.listDir("/", output, 3);
      Serial.println(output);

      item_str = sdcard.readJSONfile(SYS_CRED_FILENAME, "wifi", "ssid");
      if(item_str) {                      // check for error (null ptr)    
         strncpy(wifi_ssid, item_str, sizeof(wifi_ssid));
         Serial.printf("wifi ssid= %s\n", wifi_ssid);           
      } else 
         wifi_ssid[0] = 0x0;
         // strcpy(wifi_ssid, "");

      item_str = sdcard.readJSONfile(SYS_CRED_FILENAME, "wifi", "password");
      if(item_str) {                        // check for error (null ptr)
         strncpy(wifi_password, item_str, sizeof(wifi_password));
         Serial.printf("wifi password= %s\n", wifi_password);              
      } else 
         wifi_password[0] = 0x0;
         // strcpy(wifi_password, "");
   }

   /**
    * Initialize audio devices
    */
   audio.init(16000);                     // init mic & speaker devices with sample rate

   /**
    * Try to connect to the local WiFi router
    */
   wifi_timeout = millis();
   if(strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
      WiFi.begin(wifi_ssid, wifi_password); // SSID & Password need to be setup before this will work
      Serial.printf("\nWiFi Connecting to %s\n", wifi_ssid);
   } else {
      Serial.println("Error: WiFi credentials not defined!");
   }
   chatGPT.initChatGPT();    // setup chat gpt api key, endpoint, etc.
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t gp_timer = millis();
   float tempc;

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
      // tempc = sys_utils.RTCGetTempC();
      // Serial.printf("Board Temp C = %.2f\n", tempc);
   }

   /**
    * Check for events from GUI (button clicks, etc.)
    */
   int16_t gev = getGUIevents();     
   const char *req_text;
   switch(gev) {
      // Start a new recording for STT demo
      case GUI_EVENT_START_CHAT:
         req_text = getKeyboardText();
         Serial.println("Start new chat question:\n");
         lv_timer_handler();              // refresh graphics
         chatGPT.chatRequest(req_text, false, 40);
         break;
   }

}              // ### End of loop()



