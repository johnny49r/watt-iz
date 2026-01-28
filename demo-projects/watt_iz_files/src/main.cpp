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

// WiFi credentials
char wifi_ssid[40];
char wifi_password[40];

SD_FILE_SYS sd;

/********************************************************************
 * setup()
 */
void setup(void) 
{
   char ddlist[100];                      // cString for file info dropdown list
   char strtmp[30];
   int16_t nlbeg;
   int16_t nlend = 0;
   char *item_str;
   uint16_t btag;

   /**
    * GPIO pin assignments
    */
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);   // set as input w/pullup resistor
   pinMode(PIN_LCD_TIRQ, INPUT_PULLUP);   // resistive touch IRQ pin
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off  

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(1000);
   Serial.println("Booting...");

   /**
    * Initialize NeoPixel lib
    */
   strip.Begin();
   strip.SetPixelColor(0, gray);   
   strip.Show();                          // leds off

   /** ==============================================================
    * @brief System Initialization should be done in the correct order.
    * 1) Initialize the graphics driver and lvgl system.
    * 2) Initialize the SD File System which registers lvgl abstract 
    *    file system.
    * 3) Initialize the default lvgl styles.
    * 4) Then continue to build GUI, etc.
    =================================================================*/

   /**
    * @brief 1) Initialize graphics driver and LVGL graphics system. NOTE: This 
    * must be done before initializing the SD file system.
    */             
   guiInit();

   /**
    * @brief 2) Initialize the SD File System. NOTE: This is done after initializing
    * LVGL but before building the GUI.
    */
   bool sd_ok;
   for(int i=0; i<3; i++) {
      // sd_ok = init_sd_and_register_lvgl_fs();
      sd_ok = sd.init();  //init_sd_and_register_lvgl_fs();
      if(!sd_ok) {
         sd.deInit();
      } else   
         break;
   }

   /**
    * @brief 3) Set LVGL style objects. Must be done before creating graphics.
    */
   setDefaultStyles();                    // init lvgl widget styles

   /**
    * @brief Build tileview pages and demo graphics
    */
   demoBuilder();                         // prebuild graphics pages
   setBacklight(255);                     // OK wake up the display!

   /**
    * @brief Print SD Card info to the console 
    */
   // #define RECURSIVE_BUF_SZ   4096        // big enough for max listing chars
   // PsBuf out;
   // out.p = (char*) heap_caps_malloc(RECURSIVE_BUF_SZ, MALLOC_CAP_SPIRAM);
   // out.cap = RECURSIVE_BUF_SZ;
   // out.len = 0;
   // out.p[0] = '\0';                       // terminate empty str

   // char *from;
   // char *to;   
   
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

            // sd.listDirectory("/", out, 0, 10); 
            // // fs_list_dir_recursive("/", out, 0, 10);           
            //    Serial.printf("out= %s\n", out);
            // // parse the output string from the recursive list & fill gui file list
            // from = out.p;
            // while (true) {
            //    to = strchr(from, '\n');   // find the newline in string
            //    if(!to)
            //       break;                  // if none, all done
            //    to[0] = '\0';              // replace null term where NL was
            //    if(from[0] == 'D') {       // directory?
            //       from = strchr(from, '/');  // locate leading slash
            //       if(from)
            //          fileListAddDir(from);   // send dir name to gui
            //    }
            //    else if(from[0] == 'F') {  // file?
            //       from = strchr(from, '/');  // locate leading slash
            //       if(from) {     
            //          fileListAddFile(from);  // send file name to gui
            //       }
            //    }
            //    from = to +1;              // leapfrog to next line
            // }
            // free(out.p);                  // free PSRAM memory 
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

   } 

   switch(sd_card_info.error_code) {
      case SD_OK:
         // Display card type 
         Serial.print("SD Card Type: ");
         // determine card type
         switch(sd_card_info.card_type) {
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
         sprintf((char *)&strtmp, "\nCapacity: %d MB", sd_card_info.card_size / 1048576);
         Serial.print(strtmp);
         strcat(ddlist, strtmp);
         sprintf((char *)&strtmp, "\nAvailable: %d MB", sd_card_info.bytes_avail / 1048576);
         Serial.print(strtmp);
         strcat(ddlist, strtmp);         
         sprintf((char *)&strtmp, "\nUsed: %.2f MB\n", float(sd_card_info.bytes_used) / 1048576.0F);         
         Serial.print(strtmp); 
         strcat(ddlist, strtmp);           
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

   /**
    * @brief Check if there is a firmware upgrade to be loaded 
    */
   if(sd_ok) {
      if(sd.fexists(FIRMWARE_UPGRADE_FILENAME)) {
         openMessageBox(LV_SYMBOL_DOWNLOAD " Firmware Update Available", 
               "Press 'OK' to update or 'CANCEL' to ignore.", "OK", 
               MBOX_BTN_OK, "CANCEL", MBOX_BTN_CANCEL, "", MBOX_BTN_NONE); 
         while(true) { 
            btag = getMessageBoxBtn();
            if(btag != MBOX_BTN_NONE) 
               break;
            lv_timer_handler();
            vTaskDelay(10);
         }

         if(btag == MBOX_BTN_OK) {
            openMessageBox(LV_SYMBOL_DOWNLOAD " Firmware Update Downloading ", 
                  "Please wait until update has completed. The system will then reboot.", "", 
                  MBOX_BTN_NONE, "", MBOX_BTN_NONE, "", MBOX_BTN_NONE);     
            lv_timer_handler();           // draw the new message box
            vTaskDelay(1000);              
            if (fw_update_from_sd_wrapper(FIRMWARE_UPGRADE_FILENAME)) {
               strncpy(strtmp, FIRMWARE_UPGRADE_FILENAME, sizeof(strtmp));
               strcat(strtmp, ".used");   // rename the file so it doesn't try to load again
               if(sd.fexists(strtmp))     // delete this filename?
                  sd.fremove(strtmp);
               if(!sd.frename(FIRMWARE_UPGRADE_FILENAME, strtmp)) { // rename failed?
                  Serial.print(F("Failed to rename file: "));
                  Serial.println(strtmp);
               }
               Serial.println(F("FW Update OK! Rebooting..."));
               vTaskDelay(2000);
               ESP.restart();
            } else {
               Serial.println(F("FW Update Failed! Continue normal boot..."));
               closeMessageBox();
            }
         }
      }
   }   

   // updateDDList((char *)&ddlist);

   item_str = sd.readJsonFile(SYS_CRED_FILENAME, "wifi", "ssid");
   if(item_str) {                      // check for error (null ptr)    
      strncpy(wifi_ssid, item_str, sizeof(wifi_ssid));
      Serial.printf("wifi ssid= %s\n", wifi_ssid);           
   } else 
      wifi_ssid[0] = 0x0;

   item_str = sd.readJsonFile(SYS_CRED_FILENAME, "wifi", "password");
   if(item_str) {                        // check for error (null ptr)
      strncpy(wifi_password, item_str, sizeof(wifi_password));
      Serial.printf("wifi password= %s\n", wifi_password);              
   } else 
      wifi_password[0] = 0x0; 
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 10;
   static uint32_t lv_timer_ms = millis();
   static uint32_t gp_timer = millis();
   static uint32_t mem_timer = millis();
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
         sd_speed = sd.speedTest(SD_TEST_SEQUENTIAL);
         if(sd_speed) {
            updateWriteSpeed(sd_speed->wr_mbs);
            updateReadSpeed(sd_speed->rd_mbs);
         }
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
         sd.formatDrive();
         // fs_formatSD();                   // format SD w/FaT32
         enabFormatButn();                // re-enable format butn to show format complete              
         break;

      case GUI_EVENT_CREATE_CCF:
         // Write a new config file. Values are derived from 'credentials.h'
         ret = writeConfigFile("wattiz_config.json");
         if(!ret)
            Serial.println(F("Error: Failed to write config file!"));
         break;
   }

   if(millis() - mem_timer > 1000) {
      mem_timer = millis();
      // Serial.printf("Total heap: %d\n", ESP.getHeapSize());
      // Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
      // Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
      // Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());   
      // Serial.println("");
   }
}


/********************************************************************
 * @brief Write a config file to the SD Card. ###### Requirements: 
 * 1) credentials.h file must be present in the /src directory.
 * 2) User must edit the credential header file to include wifi ssid,
 *    password, and any necessary speech service API keys (google,
 *    openAI, etc).
 * This function creates a JSON structure and inserts the credentials
 * where appropriate, then writes to the specified file (typically 
 * "wattiz_config.json").
 */
bool writeConfigFile(const char *filename)
{
   int16_t pos = 0;
   if(sd.fexists("/credentials.txt")) {
      String CREDS;
      String SUB_STR;
      File _file = sd.fopen("/credentials.txt", FILE_READ, false);
      if(!_file)
         return false;

      // Read creadential file into String object CREDS
      while (_file.available()) {
         CREDS += (char)_file.read();
      }
      sd.fclose(_file);                   // close file

      // Normalize any path irregularities
      const char *spath = sd.normalizePath(filename);
      String out;
      String Value;

      // Build the JSON document 
      JsonDocument doc;
      doc.clear();
      doc["version"] = 2;
      doc["device"]["system_id"] = "Abbycus Watt-IZ V1.3"; // arbitrary identifier

      // Extract WiFi SSID name
      if(searchKeys(CREDS, "WIFI_SSID", Value)) {
         doc["wifi"]["ssid"] = Value;      
      } 
      // Extract WiFi Password
      if(searchKeys(CREDS, "WIFI_PASSWORD", Value)) {
         doc["wifi"]["password"] = Value;      
      }
      // Extract Google TTS Endpoint String
      if(searchKeys(CREDS, "GOOGLE_TTS_ENDPOINT", Value)) {
         doc["google"]["api_tts_endpoint"] = Value; 
      }
      // Extract Google TTS Endpoint String
      if(searchKeys(CREDS, "GOOGLE_STT_ENDPOINT", Value)) {
         doc["google"]["api_stt_endpoint"] = Value; 
      }
      // Extract Google Translate Endpoint String
      if(searchKeys(CREDS, "GOOGLE_TRANSLATE_ENDPOINT", Value)) {
         doc["google"]["api_translate_endpoint"] = Value; 
      }
      // Extract Google API Key String
      if(searchKeys(CREDS, "GOOGLE_API_KEY", Value)) {
         doc["google"]["api_key"]  = Value; 
      }   
      // Extract Google Server String
      if(searchKeys(CREDS, "GOOGLE_SERVER", Value)) {
         doc["google"]["api_server"] = Value; 
      }  
      // Extract openAI Endpoint String
      if(searchKeys(CREDS, "OPENAI_ENDPOINT", Value)) {
         doc["openai"]["api_endpoint"]  = Value; 
      }  
      // Extract openAI API Key String
      if(searchKeys(CREDS, "OPENAI_API_KEY", Value)) {
         doc["openai"]["api_key"] = Value; 
      }    
      // Extract openAI Chat Version String
      if(searchKeys(CREDS, "OPENAI_CHAT_MODEL", Value)) {
         doc["openai"]["chat_version"] = Value; 
      }    
      // Timestamp      
      doc["updated_at"] = "Wed, Jan 28, 2026 @ 15:57";   // datetime
      
      size_t n = serializeJsonPretty(doc, out); // make sanitized JSON string
         // Serial.print("out= ");
         // Serial.println(out);
      if(n <= 0)                             // serialize fail?
         return false;

      // Write the JSON file and exit
      if(sd.writeFile(spath, (uint8_t *)out.c_str(), out.length())) {
         Serial.println("Write config complete!");
         return true;
      }
      return false;
   }
   else 
      return false;
}


/********************************************************************
 * @brief Search for string 'key' in the source file. If key is a match,
 *  return value string.
 * @return true if the key is found.
 */
bool searchKeys(String &src, String key, String &value)
{
   String aLine;
   int16_t pos = 0;

   while(nextLine(src, pos, aLine)) {
      if(aLine.startsWith(key)) {
         parseKeyQuotedValue(aLine, key, value); 
         break;
      }
   }
   return true;
}


/********************************************************************
 * @brief Return the value string of the key/value pair
 */
bool parseKeyQuotedValue(const String &line, String &keyOut, String &valOut)
{
   keyOut = "";
   valOut = "";

   if (!line.length()) return false;

   // Skip comments (optional)
   if (line.startsWith("#") || line.startsWith("//")) return false;

   // Find first quote
   int q1 = line.indexOf('"');
   if (q1 < 0) return false;

   // Key is everything before first quote; take first token
   String left = line.substring(0, q1);
   left.trim();
   if (!left.length()) return false;

   int sp = left.indexOf(' ');
   int tab = left.indexOf('\t');
   int sep = -1;

   if (sp >= 0 && tab >= 0) sep = min(sp, tab);
   else if (sp >= 0)        sep = sp;
   else if (tab >= 0)       sep = tab;

   keyOut = (sep >= 0) ? left.substring(0, sep) : left;
   keyOut.trim();
   if (!keyOut.length()) return false;

   // Extract quoted value
   int q2 = line.indexOf('"', q1 + 1);
   if (q2 < 0) return false;

   valOut = line.substring(q1 +1, q2);
   return true;
}


/********************************************************************
 * @brief Return text lines in the source text file    
 */
bool nextLine(const String &src, int16_t &pos, String &line)
{
    if (pos >= src.length()) return false;

    int end = src.indexOf('\n', pos);
    if (end < 0) {
        line = src.substring(pos);
        pos = src.length();
    } else {
        line = src.substring(pos, end);
        pos = end + 1;
    }

    line.replace("\r", "");   // handle Windows CRLF
    line.trim();              // trim leading/trailing whitespace
    return true;
}


/********************************************************************
 * @brief Function to update firmware from a *.bin file on the SD card.
 * @note: The file should be located on path "/update/firmware.bin"
 */
bool fw_update_from_sd_wrapper(const char* path)
{
   // 1) Open firmware file
   File file = sd.fopen(path, FILE_READ, false);
   if (!file) {
      Serial.printf("OTA: cannot open %s\n", path);
      return false;
   }

   // 2) Determine file size
   const uint32_t binSize = (uint32_t)file.size();
   Serial.printf("OTA: file size: %u bytes\n", (unsigned)binSize);

   if (binSize < 32768) {                 // min valid file size
      Serial.println("OTA: file too small; abort");
      file.close();
      return false;
   }

   // 3) Select target OTA partition (inactive slot)
   const esp_partition_t* running = esp_ota_get_running_partition();
   const esp_partition_t* target  = esp_ota_get_next_update_partition(NULL);

   if (!target) {
      Serial.println("OTA: no OTA target partition");
      file.close();
      return false;
   }

   Serial.printf("OTA: running=%s @0x%08lx size=%lu\n",
            running ? running->label : "?",
            running ? (unsigned long)running->address : 0UL,
            running ? (unsigned long)running->size : 0UL);

   Serial.printf("OTA: target =%s @0x%08lx size=%lu\n",
            target->label,
            (unsigned long)target->address,
            (unsigned long)target->size);

               // debugging =========================================
               const esp_partition_t* configured = esp_ota_get_boot_partition();
               const esp_partition_t* boot       = esp_ota_get_boot_partition(); // (same API name; kept for clarity)

               Serial.printf("OTA: configured boot=%s @0x%08lx subtype=%d\n",
                     configured ? configured->label : "?",
                     configured ? (unsigned long)configured->address : 0UL,
                     configured ? configured->subtype : -1);

               Serial.printf("OTA: running: label=%s addr=0x%08lx size=0x%lx subtype=%d\n",
                     running ? running->label : "?",
                     running ? (unsigned long)running->address : 0UL,
                     running ? (unsigned long)running->size : 0UL,
                     running ? running->subtype : -1);

               Serial.printf("OTA: target : label=%s addr=0x%08lx size=0x%lx subtype=%d\n",
                        target ? target->label : "?",
                        target ? (unsigned long)target->address : 0UL,
                        target ? (unsigned long)target->size : 0UL,
                        target ? target->subtype : -1);

               // ===================================================

   if (binSize > target->size) {
      Serial.println("OTA: bin does not fit target partition");
      file.close();
      return false;
   }

   if (running && target->address == running->address) {
      Serial.println("OTA: target equals running partition (unexpected); abort");
      file.close();
      return false;
   }

   // 4) Begin OTA session
   esp_ota_handle_t h = 0;
   esp_err_t err = esp_ota_begin(target, binSize, &h);
   if (err != ESP_OK) {
      Serial.printf("OTA: esp_ota_begin failed: %s\n", esp_err_to_name(err));
      file.close();
      return false;
   }

   // 5) Stream SD file -> esp_ota_write
   constexpr uint32_t BUF_SZ = 8192;
   uint8_t* buf = (uint8_t*)heap_caps_malloc(BUF_SZ, MALLOC_CAP_8BIT);
   if (!buf) {
      Serial.println("OTA: malloc failed");
      esp_ota_end(h);
      file.close();
      return false;
   }

   uint32_t offset = 0;
   uint32_t lastPct = 0;

   while (offset < binSize) {
      uint32_t toRead = binSize - offset;
      if (toRead > BUF_SZ) toRead = BUF_SZ;

      uint32_t bytesRead = sd.fread(file, buf, toRead, offset);
      if (bytesRead == 0) {
         Serial.printf("OTA: fread returned 0 at offset=%u\n", (unsigned)offset);
         heap_caps_free(buf);
         esp_ota_end(h);
         file.close();
         return false;
      }

      err = esp_ota_write(h, buf, bytesRead);
      if (err != ESP_OK) {
         Serial.printf("OTA: esp_ota_write failed at offset=%u: %s\n",
                     (unsigned)offset, esp_err_to_name(err));
         heap_caps_free(buf);
         esp_ota_end(h);
         file.close();
         return false;
      }

      offset += bytesRead;

      // progress every 5%
      uint32_t pct = (uint32_t)((offset * 100ULL) / binSize);
      if (pct != lastPct && (pct % 5 == 0)) {
         Serial.printf("OTA: %u%%\n", (unsigned)pct);
         lastPct = pct;
      }
   }

   heap_caps_free(buf);
   file.close();

   // 6) Finalize OTA
   err = esp_ota_end(h);
   if (err != ESP_OK) {
      Serial.printf("OTA: esp_ota_end failed: %s\n", esp_err_to_name(err));
      return false;
   }

   // 7) Switch boot partition
   err = esp_ota_set_boot_partition(target);
   if (err != ESP_OK) {
      Serial.printf("OTA: set boot partition failed: %s\n", esp_err_to_name(err));
      return false;
   }

   Serial.println("OTA: update written and boot partition set");
   return true;
}
