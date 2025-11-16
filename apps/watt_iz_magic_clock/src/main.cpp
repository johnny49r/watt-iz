/**
 * main.cpp
 */
#include "main.h"


// WS2812 RGB Led (NEOPIXEL)
#define pixelCount         1
NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(pixelCount, PIN_NEO_PIXEL);

#define colorSaturation 50    // NEOPIXEL brightness 0 - 255

// Basic NEOPIXEL colors
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor cyan(0, colorSaturation, colorSaturation * 0.6);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor orange(colorSaturation, int(float(colorSaturation * 0.7)), int(float(colorSaturation * 0.4)));
RgbColor gray(20, 20, 20);

// WiFi credentials
char wifi_ssid[40];
char wifi_password[60];
char *item_str;   

// globals
uint16_t loop_state;


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
   vTaskDelay(100);
   Serial.println("\n");

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, cyan);   
   strip.Show();   
   
   /**
    * Initialize PSRAM
    */
   psramInit();   

   /**
    * Initialize graphics, backlight dimming, touch screen, and demo widgets
    */                    
   if(!guiInit()) {
      strip.SetPixelColor(0, red);   
      strip.Show();        
      Serial.println("ERROR: Failed to Init GUI");
   } else {
      // Set LVGL style objects. Must be done before creating graphics.
      setDefaultStyles();

      // Build tileview pages and create graphics
      pageBuilder(); 
   }

   /**
    * @brief Print memory usage
    */
   Serial.printf("\nTotal heap: %d\n", ESP.getHeapSize());
   Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
   Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
   Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());   
   Serial.println("");     

   /**
    * Try to initialize the SD card
    */
   bool sd_ok;;
   for(int i=0; i<3; i++) {
      sd_ok = sd.init();
      if(sd_card_info.error_code == NOT_INSERTED)  // don't bother to unmount
         break;
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
            Serial.println(F("ERROR: SD is not yet initialized!"));
            break;

         case MOUNT_FAILED:     
            Serial.println(F("ERROR: SD Card failed to mount!"));
            break;

         case NOT_INSERTED:         
            Serial.println(F("ERROR: SD Card not inserted!"));
            break;

         case CARD_FULL:        
            Serial.println(F("ERROR: SD Card is full - no space available!"));
            break;
      }    
   } else {
      Serial.println(F("Error: SD Card Initialization Failed!"));
      strip.SetPixelColor(0, red);   
      strip.Show();
   }

   if(sd_card_info.error_code == SD_OK) {
      // Read WiFi ssid & password from SD card.
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
   }

   /**
    * @brief Initialize the RTC chip - DS3231
    */
   if(!sys_utils.RTCInit(PIN_I2C_SDA, PIN_I2C_SCL)) {
      Serial.println(F("ERROR: Failed to init RTC Chip!"));
   } 

   /**
    * @brief Initialize audio class
    */
   if(!audio.init(AUDIO_SAMPLE_RATE)) {
      Serial.println(F("Error: Failed audio.init()!"));
   }

   /**
    * @brief Initialize Chat GPT 
    */
   if(!chatGPT.init()) {                  // setup chat gpt api key, endpoint, etc.
         Serial.print("f\n");
      Serial.println(F("Error: Failed chatGPT.init()!"));
   }

   /**
    * Initialize Speech To Text credentials
    */
   if(!speechToText.init()) {
      Serial.println(F("Error: Failed speechToText.init()!"));
   }

   /**
    * Initialize Text To Speech credentials
    */
   if(!textToSpeech.init()) {
      Serial.println(F("Error: Failed textToSpeech.init()!"));      
   }

   /**
    * @brief Initialize Language Translate Service
    */
   if(!language_xlate.init()) {
      Serial.println(F("Error: Failed language_xlate.init()!"));  
   }

   /**
    * @brief Start WiFi connection
    */
   if(strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
      WiFi.begin(wifi_ssid, wifi_password); // SSID & Password need to be setup before this will work
      WiFi.setSleep(WIFI_PS_NONE);        // no power saving mode - keep wifi active
      Serial.printf("\nWiFi Connecting to %s", wifi_ssid);

   } else {
      Serial.println("Error: WiFi credentials not defined!");
   }   

   loop_state = STATE_NONE;
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t progress_timer = millis();
   static uint8_t last_second = 0;

   static rec_status_t recording_status = {
         .status = REC_STATUS_NONE,
         .recorded_frames = 0,
         .max_frames = 0};
   static rec_status_t *pRecStatus = nullptr;
   static int8_t progress = 0;
   static int8_t last_progress = 0;
   static int16_t gui_event_copy = 0;
   static uint8_t lang_from_index;  // currently selected 'from' index
   static uint8_t lang_to_index;    // currently selected 'to' index
   bool tok;
   String ChatResponse;
   String TranslateResponse;

#define TEXT_BUFR_SIZE     2048           // make big enough for target text    
   static char *txt_bufr = (char *)heap_caps_malloc(TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   static char *out_bufr = (char *)heap_caps_malloc(TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); 

   /**
    * Service the lvgl graphics engine at periodical intervals (typical = 10ms)
    */
   if(millis() - lv_timer_ms > lv_next_interval) {
      // Refresh graphics engine and predict next time to call this function.
      lv_next_interval = lv_timer_handler();    // returns next interval in ms
      lv_timer_ms = millis();
   }

   /**
    * @brief Service GUI Events
    */
   int16_t gui_event = getGUIevents();   
   switch(gui_event) {
      case GUI_EVENT_TRANSLATE:        
         lang_from_index = getLangIndex(ROLLER_FROM);
         lang_to_index = getLangIndex(ROLLER_TO); 
      case GUI_EVENT_CHAT_REQ:   
         last_progress = -1;
         gui_event_copy = gui_event;               // copy of gui event    
         if(gui_event_copy == GUI_EVENT_CHAT_REQ)
            sd.fremove(CHAT_RESPONSE_FILENAME);    // delete previous chat response audio
         if(gui_event_copy == GUI_EVENT_TRANSLATE) {

         }
         audio.playTone(2400, 16000, 40, 0.16, true); // beep notify recording has started
         vTaskDelay(50);                  // don't record beep sound
         audio.clearReadBuffer();         // remove any garbage in mic-bufr pipeline  
         if(gui_event_copy == GUI_EVENT_CHAT_REQ)
            audio.startRecording(8.0, true, false, CHAT_REQUEST_FILENAME);  // 8 sec voice rec   
         else if(gui_event_copy == GUI_EVENT_TRANSLATE)
            audio.startRecording(8.0, true, false, TRANSLATE_REQUEST_FILENAME);  // 8 sec voice rec          
         loop_state = STATE_VOICE_RECORDING; 
         progress_timer = millis();
         break;


   }

   /**
    * @brief Service Loop States
    */
   switch (loop_state) {
      case STATE_VOICE_RECORDING:
         if((millis() - progress_timer) > 200) {   // chect again after mic frame time
            progress_timer = millis();             
            pRecStatus = audio.getRecordingStatus();  // get status from recording task           
            memcpy(&recording_status, pRecStatus, sizeof(rec_status_t)); // local copy of status                
            if((recording_status.status & REC_STATUS_BUSY) == 0) {   // ok to do if not busy
               if((recording_status.status & REC_STATUS_REC_CMPLT) == 0) { // not finished yet...
                  // Update the GUI progress arc
                  progress = map(recording_status.recorded_frames, 0, recording_status.max_frames, 0, 100);
                  if(progress != last_progress) {
                     last_progress = progress;
                     if(gui_event_copy == GUI_EVENT_CHAT_REQ) {
                        updateChatProgress(progress);
                     }
                     else if(gui_event_copy == GUI_EVENT_TRANSLATE) 
                        updateTranslateProgress(progress);
                  } 
               } else {                   // end of recording
                        Serial.printf("End of recording, copy=%d, stat=%x\n", gui_event_copy, recording_status.status);
                  if(gui_event_copy == GUI_EVENT_CHAT_REQ) {   // process chat request
                     updateChatProgress(100);
                     loop_state = STATE_SPEECH_TO_TEXT; 
                  }
                  else if(gui_event_copy == GUI_EVENT_TRANSLATE) {   // process translate
                     updateTranslateProgress(100);
                     loop_state = STATE_SPEECH_TO_TEXT;                      
                  }
                  else 
                     loop_state = STATE_NONE;
               }
            } 
         }
         break;

      case STATE_SPEECH_TO_TEXT:   
         if(gui_event_copy == GUI_EVENT_CHAT_REQ) {
            tok = speechToText.transcribeSpeechToText(CHAT_REQUEST_FILENAME, STT_LANGUAGE); // assume english
            loop_state = STATE_CHAT_REQUEST;    // next step in the chat process            
         }
         else if(gui_event_copy == GUI_EVENT_TRANSLATE) {
            tok = speechToText.transcribeSpeechToText(TRANSLATE_REQUEST_FILENAME, LanguageArray[lang_from_index].lang_code); // assume english 
            loop_state = STATE_TRANSLATE_REQUEST;                                 
         }
         if(!tok) {
            speechToText.sttResponse = "No recognizable speech!";            
            openMessageBox(LV_SYMBOL_WARNING " STT Error!", speechToText.sttResponse.c_str(), "OK", ""); 
            updateChatProgress(-1);       // reset butn text to "Start"  
            loop_state = STATE_NONE;  
         } 
         speechToText.sttResponse.toCharArray(txt_bufr, TEXT_BUFR_SIZE);           
         break;

      case STATE_CHAT_REQUEST:
         Serial.println(speechToText.sttResponse);
         ChatResponse = chatGPT.chatRequest(speechToText.sttResponse.c_str(), true, 15);
         if(ChatResponse.isEmpty()) {
            openMessageBox(LV_SYMBOL_WARNING " ChatGPT Error!", "No response!", "OK", "");
         }
         loop_state = STATE_NONE;   
         updateChatProgress(-1);          // reset butn text to "Start"          
         break;

      case STATE_TRANSLATE_REQUEST:
         // begin language translation text->text
                  Serial.printf("%s\n", txt_bufr);
         TranslateResponse = language_xlate.translateLanguage(txt_bufr, 
               LanguageArray[lang_from_index].lang_code,
               LanguageArray[lang_to_index].lang_code);

                     Serial.printf("tresp=%s\n", TranslateResponse.c_str());
         if(!TranslateResponse.isEmpty()) {
            TranslateResponse.toCharArray(out_bufr, TEXT_BUFR_SIZE);
                     Serial.printf("from index=%d, to index=%d, out_bufr=%s\n", lang_from_index, lang_to_index, out_bufr);
            setXToText((char *)out_bufr, lang_to_index);    // draw response text in gui
            // vocalize the translated text
            textToSpeech.transcribeTextToSpeech(TranslateResponse, true, TRANSLATE_REQUEST_FILENAME, 40, 
                  LanguageArray[lang_to_index].lang_code,
                  LanguageArray[lang_to_index].voice_name,
                  LanguageArray[lang_to_index].speakingRate);
         }      
         loop_state = STATE_NONE;                   
         break;
   }

}



