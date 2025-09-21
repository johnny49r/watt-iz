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

char demo_message[] = {"Hello. This is the Abbycus E-S-P 32 speech enabled development board. " 
      "Please check our github website for more information @ https://github.com/johnny49r/watt-iz"};


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
         strcpy(wifi_ssid, "");

      item_str = sdcard.readJSONfile(SYS_CRED_FILENAME, "wifi", "password");
      if(item_str) {                        // check for error (null ptr)
         strncpy(wifi_password, item_str, sizeof(wifi_password));
         Serial.printf("wifi password= %s\n", wifi_password);              
      } else 
         strcpy(wifi_password, "");
   }

   /**
    * Initialize audio devices
    */
   audio.init(16000);

   /**
    * Initialize Speech To Text credentials
    */
   speechToText.init();

   /**
    * Try to connect to the local WiFi router
    */
   wifi_timeout = millis();
   if(strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
      WiFi.begin(wifi_ssid, wifi_password); // SSID & Password need to be setup before this will work
      Serial.printf("\nWiFi Connecting to %s", wifi_ssid);

   } else {
      Serial.println("Error: WiFi credentials not defined!");
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
   float tempc;
   static rec_status_t recording_status = {
         .status = REC_STATUS_NONE,
         .recorded_frames = 0,
         .max_frames = 0};
   static rec_status_t *pRecStatus = nullptr;
   char txt[250];
   static int8_t progress = 0;
   static int8_t last_progress = 0;
   static bool recording_in_progress = false;
   static uint32_t progress_timer = millis();
   static uint32_t keepalive_timer = millis();

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
   switch(gev) {
      // Start a new recording for STT demo
      case GUI_EVENT_START_REC:
            Serial.println("Starting new recording...");           
         setRecProgress(0);                    
         audio.clearReadBuffer();      // remove any noise prior to recording   
         // Prepare a recording for STT transcribe     
         audio.startRecording(5.0, NULL, STT_CHUNK_SIZE, "/rec_test.wav");         
         recording_in_progress = true;           
         progress_timer = millis();                 
         break;
   }

   // Read recording progress periodically (every 20ms)
   if(recording_in_progress && (millis() - progress_timer) > 20) {
      progress_timer = millis();
      pRecStatus = audio.getRecordingStatus();  // get status from recording task
      memcpy(&recording_status, pRecStatus, sizeof(rec_status_t));
   }

   /**
    * @brief Show recording progress in GUI
    */
   if(recording_status.status == REC_STATUS_PROGRESS) {
      progress = map(recording_status.recorded_frames, 0, recording_status.max_frames, 0, 100);
      if(progress != last_progress) {
         last_progress = progress;
         setRecProgress(progress);
      } 
   }
  
   /**
    * @brief If new recording has finished, call STT transcribe service
    */
   else if(recording_in_progress && recording_status.status == REC_STATUS_REC_CMPLT) {
      recording_in_progress = false;
      setRecProgress(100);                // ensure progress is 100%
      setSpeakButnText("Speak!");         // restore start recording butn
      Serial.println("Recording completed!");
      bool tok = speechToText.transcribeSpeechToText("/rec_test.wav"); 
      strncpy((char *)&txt, speechToText.sttResponse.c_str(), sizeof(txt));          
      setLabelText((char *)&txt);  
      lv_timer_handler();                 // refresh graphics (repaint speak butn)          
      if(tok) {       
         audio.playWavFromSD("/rec_test.wav", 40); // playback users recording
      } else { 
         Serial.printf("STT Error= %s\n", txt);
      }
      recording_status.status = REC_STATUS_NONE;
   }

   /**
    * @brief Keep the sttClient alive, otherwise it will timeout and go offline.
    * The problem with that is that it may take some time to return to connect.
    */
   if((millis() - keepalive_timer) > 120000) {     // 2 minute keepalive ping
      keepalive_timer = millis();
      if(!speechToText.clientKeepAlive()) {
         speechToText.clientReconnect();
      }
   }

}              // ### End of loop()



