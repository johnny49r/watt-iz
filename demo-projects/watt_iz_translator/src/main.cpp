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
   char *item_str;
   char wifi_ssid[40];
   char wifi_password[60];
   uint8_t wifi_connect_count = 0;
   uint32_t wifi_timeout = millis();
   String output;
   char strtmp[30];

   /**
    * GPIO pin assignments
    */
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(2000);
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
   }

   /**
    * Try to initialize the SD card
    */
   bool sd_ok;;
   for(int i=0; i<3; i++) {
      sd_ok = sd.init();
      if(!sd_ok) {
         sd.deInit();           // try to unmount
      } else   
         break;
   }
   if(sd_ok) {
      switch(sd_card_info.error_code) {
         case SD_OK:
            // Display card type 
            Serial.print("\nSD Card Type: ");
            // determine card type
            switch(sd_card_info.card_type) {
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
            sprintf((char *)&strtmp, "Capacity: %dMB\n", sd_card_info.card_size / 1048576);
            Serial.print(strtmp);
            sprintf((char *)&strtmp, "Available: %dMB\n", sd_card_info.bytes_avail / 1048576);
            Serial.print(strtmp);       
            sprintf((char *)&strtmp, "Used: %dMB\n\n", sd_card_info.bytes_used / 1048576);         
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

      // Read the config file for wifi credentials
      item_str = sd.readJsonFile(SYS_CRED_FILENAME, "wifi", "ssid");
      if(item_str) {                      // check for error (null ptr)    
         strncpy(wifi_ssid, item_str, sizeof(wifi_ssid));
         Serial.printf("wifi ssid= %s\n", wifi_ssid);           
      } else 
         strcpy(wifi_ssid, "");

      item_str = sd.readJsonFile(SYS_CRED_FILENAME, "wifi", "password");
      if(item_str) {                        // check for error (null ptr)
         strncpy(wifi_password, item_str, sizeof(wifi_password));
         Serial.printf("wifi password= %s\n", wifi_password);              
      } else 
         strcpy(wifi_password, "");

   } else {
      Serial.println("Error: SD Card Initialization Failed!");
      strip.SetPixelColor(0, red);   
      strip.Show();
   }

   /**
    * Initialize audio devices
    */
   audio.init(16000);

   /**
    * Initialize Speech To Text credentials
    */
   speechToText.init();
   textToSpeech.init();
   language_xlate.init();

   /**
    * @brief Start WiFi connection
    */
   wifi_timeout = millis();
   if(strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
      WiFi.begin(wifi_ssid, wifi_password); // SSID & Password need to be setup before this will work
      WiFi.setSleep(WIFI_PS_NONE);        // no power saving mode - keep wifi active
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
   // static bool do_once = true;
   float tempc;
   static rec_status_t recording_status = {
         .status = REC_STATUS_NONE,
         .recorded_frames = 0,
         .max_frames = 0};
   static rec_status_t *pRecStatus = nullptr;
#define TEXT_BUFR_SIZE     2048           // make big enough for target text    
   static char *txt_bufr = (char *)heap_caps_malloc(TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   static char *out_bufr = (char *)heap_caps_malloc(TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   
   static bool begin_lang_translate = false;
   static int8_t progress = 0;
   static int8_t last_progress = 0;
   static bool recording_in_progress = false;
   static uint32_t progress_timer = millis();
   String xlate;
   static uint8_t lang_from_index;  // currently selected 'from' index
   static uint8_t lang_to_index;    // currently selected 'to' index

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
         audio.clearReadBuffer();      // remove any garbage in mic bufr pipeline   
         // Capture current language indexes
         lang_from_index = getLangIndex(ROLLER_FROM);
         lang_to_index = getLangIndex(ROLLER_TO);         
  
         // Begin a recording for STT transcribe          
         audio.startRecording(6.0, true, true, "/rec_test.wav");           
         recording_in_progress = true;           
         progress_timer = millis();                 
         break;

      case GUI_EVENT_STOP_REC:
         if(recording_in_progress) {
            audio.stopRecording();
            recording_in_progress = false;
            setRecProgress(100);
         }
         break;
   }

   /**
    * @brief Get progress value from background recording
    */
   if(recording_in_progress && (millis() - progress_timer) > 50) {
      progress_timer = millis();
      pRecStatus = audio.getRecordingStatus();  // get status from recording task
      memcpy(&recording_status, pRecStatus, sizeof(rec_status_t));
   }

   /**
    * @brief Show recording progress in GUI
    */
   if(recording_status.status == REC_STATUS_RECORDING) {
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
      setSpeakButnText("Translate");      // restore "Translate" text on GUI butn
      Serial.println("Recording completed!");
      bool tok = speechToText.transcribeSpeechToText("/rec_test.wav", LanguageArray[lang_from_index].lang_code); 
      if(!tok)
         speechToText.sttResponse = "Error: No recognizable speech!";
      speechToText.sttResponse.toCharArray(txt_bufr, TEXT_BUFR_SIZE);       
      setXFromText((char *)txt_bufr, lang_from_index); 
      strcpy(out_bufr, "");
      setXToText((char *)out_bufr, lang_to_index);         
      if(tok) {      
         begin_lang_translate = true; 
         // audio.playWavFile("/rec_test.wav", 40); // playback users recording
      } 
      recording_status.status = REC_STATUS_NONE;
   }

   /**
    * @brief Begin language translation
    */
   if(begin_lang_translate) {
      begin_lang_translate = false;       // reset translation flag

      xlate = language_xlate.translateLanguage(txt_bufr, 
               LanguageArray[lang_from_index].lang_code,
               LanguageArray[lang_to_index].lang_code);

      if(!xlate.isEmpty()) {
         xlate.toCharArray(out_bufr, TEXT_BUFR_SIZE);
         setXToText((char *)out_bufr, lang_to_index);    // draw response text in gui
         textToSpeech.transcribeTextToSpeech(xlate, true, "/xlate_voice.wav", 40, // 
                  LanguageArray[lang_to_index].lang_code,
                  LanguageArray[lang_to_index].voice_name,
                  LanguageArray[lang_to_index].speakingRate);
      }
   }
}              // ### End of loop()



