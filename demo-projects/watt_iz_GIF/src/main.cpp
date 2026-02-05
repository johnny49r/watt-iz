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
RgbColor gray(4, 4, 4);

/********************************************************************
 * setup()
 */
void setup(void) 
{
   uint16_t btag;
   char strtmp[30];

   Serial.begin(115200);                  // init serial monitor 
   vTaskDelay(200);
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
    * Initialize PSRAM
    */
   psramInit();

   /**
    * Initialize graphics, backlight dimming, touch screen, and demo widgets
    */                    
   guiInit();

   /**
    * Set LVGL style objects. Must be done before creating graphics.
    */
   setDefaultStyles();

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

   /**
    * Build tileview pages and demo graphics
    */
   pageBuilder();      

   /**
    * @brief Print memory usage
    */
   Serial.printf("\nTotal heap: %d\n", ESP.getHeapSize());
   Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
   Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
   Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());   
   Serial.println("");   

   /**
    * @brief Check if there is a firmware upgrade to be loaded 
    */
   if(false) { //sd_ok) {
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
}


/********************************************************************
 * loop()
 */
void loop() 
{
   static uint32_t lv_next_interval = 5;     // initial refresh period
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
