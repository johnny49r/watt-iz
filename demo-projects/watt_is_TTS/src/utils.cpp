/**
 * utils.cpp
 */
#include "utils.h"

SYS_UTILS sys_utils;                      // instance of utils functions
TimerManager TimerMgr;                    // instance of timer class 

RtcDS3231<TwoWire> Rtc(Wire);             // Using this RTC lib cuz Adafruit lib didn't work!

SYS_UTILS::SYS_UTILS(void)
{
   NVS_OK = false;
}

TimerHandle_t  timers[MAX_TIMERS];
uint32_t       expiryTicks[MAX_TIMERS];   // for remaining time
bool           timerActive[MAX_TIMERS];
uint8_t        timerID;


void unifiedTimerCallback(TimerHandle_t xTimer) {
    // Recover logical ID from timer's ID
    intptr_t id = (intptr_t) pvTimerGetTimerID(xTimer); // cast void* → integer

    if (id < 0 || id >= MAX_TIMERS) {
        return;
    }

    timerActive[id] = false;  // mark inactive

    // Do your per-timer work here.
    // In practice: send event to a queue, flip a flag, etc.
    Serial.printf("Timer %d expired!\n", (int)id);
}


/**
 * @brief Non class function called when timer completes
 */
void genericTimerCallback(TimerHandle_t xTimer) {
    // Recover logical ID from timer's ID
    intptr_t id = (intptr_t) pvTimerGetTimerID(xTimer); // cast void* → integer

    if (id < 0 || id >= MAX_TIMERS) {
        return;
    }

    timerActive[id] = false;  // mark inactive

    // Do your per-timer work here.
    // In practice: send event to a queue, flip a flag, etc.
    Serial.printf("Timer %d expired!\n", (int)id);
}


/********************************************************************
 * @brief Initialize the NVS (Non Volatile Storage) system
 * @note: Arduino 'Preferences' doesn't work with ESP32 so this code
 * uses ArduinoNVS library for non-volatile storage.
 * @note: If 'sys_setting_t' struct changed, change NVS_KEY to load new
 * changes.
 */ 
bool SYS_UTILS::initNVS(void)
{
   uint16_t blobLength = 0;
   bool set_defaults = true;

   NVS_OK = NVS.begin();                     // init Non-Volatile storage (same as 'EEPROM')
   if(NVS_OK) {
      uint64_t key = NVS.getInt("nvs_key");  // check if flash has been initialized
      if(key == NVS_KEY) {    
         blobLength = NVS.getBlobSize("sys_set");  // get the stored struct length
         if(blobLength == sizeof(sys_settings_t)) {   // validate the data size in flash
            // save current SystemSettings to flash memory (NVS)
            NVS.getBlob("sys_set", (uint8_t *)&SystemSettings, blobLength);    // read setup data from flash
               // Serial.println(F("System info read from NV memory OK!"));
            set_defaults = false;            // defaults not needed
         }
      } 
   } 
   if(set_defaults) {
      // load sys settings with default values
      strcpy(SystemSettings.device_name, "");
      SystemSettings.screen_rotation = 1;       // 1 or 3 (landscape mode)
      SystemSettings.brightness = 80;           // default backlight brightness. 0 - 100%
      SystemSettings.microphone_volume = 33;
      // Audio default settings
      SystemSettings.speaker_volume = 33;       // default audio volume
      // Clock defaults
      SystemSettings.timezone = 7;              // default timezone (Indochina)
      SystemSettings.time_format24 = false;     // default use 12 hour time

      if(NVS_OK)
         return saveSettingsNVS(false);         // save defaults in NVS, no reboot
   }
   return true;
}


/********************************************************************
*  @brief Save the system settings to non-volatile flash memory
* 
*  @return true - save successful
*/
bool SYS_UTILS::saveSettingsNVS(bool reboot)
{
   bool ret = true;
   SystemSettings.settings_changed = false;  // reset the settings changed flag
   // Save system settings to flash
   if(!NVS.setBlob("sys_set", (uint8_t *)&SystemSettings, sizeof(sys_settings_t), true))
      return false;

   // Save key to validate system settings are stored in flash
   uint64_t key = NVS.getInt("nvs_key");     // check if flash has been initialized
   if(key != NVS_KEY) {
      ret = NVS.setInt("nvs_key", NVS_KEY, true);  // save new nvs key
   } 
   if(reboot)  {      // reboot?
      vTaskDelay(100);
      ESP.restart();
   }
   return ret;
}


/********************************************************************
 * @brief Return pointer to the device name string
 */
char * SYS_UTILS::getDeviceName(void)
{
   return (char *)&SystemSettings.device_name;
}


/********************************************************************
 * @brief Save device name string in system settings struct
 */
void SYS_UTILS::setDeviceName(const char *dev_name)
{
   strncpy((char *)&SystemSettings.device_name, dev_name, MAX_NET_NAME_CHARS);
}


/********************************************************************
 * @brief Return pointer to the WiFi SSID name
 */
char * SYS_UTILS::getWifiSSID(void)
{
   return (char *)&SystemSettings.wifi_ssid;
}


/********************************************************************
 * @brief Save wifi SSID in system settings struct
 */
void SYS_UTILS::setWifiSSID(const char *ssid)
{
   strncpy((char *)&SystemSettings.wifi_ssid, ssid, MAX_WIFI_CRED_CHARS);
}
      

/********************************************************************
 * @brief Return pointer to the WiFi password string
 */
char * SYS_UTILS::getWifiPassword(void)
{
   return (char *)&SystemSettings.wifi_password;
}


/********************************************************************
 * @brief Save wifi password in system settings struct
 */
void SYS_UTILS::setWifiPassword(const char *password)
{
   strncpy((char *)&SystemSettings.wifi_password, password, MAX_WIFI_CRED_CHARS);
}


/********************************************************************
 * @brief Initialize LCD Backlight PWM control
 */
void SYS_UTILS::initBacklight(void)
{
   // Initialize LCD GPIO's
   pinMode(PIN_LCD_BKLT, OUTPUT);         // LCD backlight PWM GPIO
   digitalWrite(PIN_LCD_BKLT, LOW);       // backlight off   
   
   // Configure TFT backlight dimming PWM
   ledcSetup(BKLT_CHANNEL, BKLT_FREQ, BKLT_RESOLUTION);
   ledcAttachPin(PIN_LCD_BKLT, BKLT_CHANNEL);  // attach the channel to GPIO pin to control dimming      
   setBrightness(sys_utils.SystemSettings.brightness);   // set system brightness
}


/********************************************************************
 * @brief Set LCD Backlight brightness
 */
void SYS_UTILS::setBrightness(uint8_t brightness)
{
   SystemSettings.brightness = brightness;  
   brightness = map(brightness, 0, 100, 0, 255);
   ledcWrite(BKLT_CHANNEL, brightness);   // set PWM value from 0 - 255
}


/********************************************************************
 * @brief Set speaker volume 0 - 100%
 */
void SYS_UTILS::setVolume(uint8_t volume)
{
   if(volume > 100) volume = 100;
   SystemSettings.speaker_volume = volume;
}


/********************************************************************
 * @brief Return current saved volume.
 */
uint8_t SYS_UTILS::getVolume(void)
{
   return SystemSettings.speaker_volume;
}


/********************************************************************
 * @brief Save new date/time values to the RTC chip DS3231
 */
void SYS_UTILS::RTCsetNewDateTime(uint16_t year, uint8_t month, uint8_t day, 
         uint8_t hour, uint8_t minute, uint8_t second)
{
   RtcDateTime updated (
   year,
   month,
   day,
   hour,
   minute,             
   second);

   // Apply the new datetime to the RTC chip
   Rtc.SetDateTime(updated);
}


/********************************************************************
 * @brief Convert Universal Time Code to a POSIX string for setting time
 */
String SYS_UTILS::makeTzString(int8_t UTC_offset)
{
   int8_t posix_offset = -UTC_offset;    // reverse sign for POSIX

   // build string: GMT+X, GMT-X, or GMT0
   String tz = "GMT";
   if (posix_offset > 0) 
      tz += "+" + String(posix_offset);
   else if (posix_offset < 0) 
      tz += String(posix_offset);   // minus already included
   else 
      tz += "0";
   return tz;
}


/********************************************************************
 * Intialize RTC chip (DS3231). Returns true if successful.
 */
bool SYS_UTILS::RTCInit(uint8_t sda, uint8_t sck)
{
   Rtc.Begin(sda, sck);
   return RTCIsRunning();
}


/********************************************************************
 * Check if RTC chip is running. Returns true if OK.
 */
bool SYS_UTILS::RTCIsRunning(void)
{
   return Rtc.GetIsRunning();
}


/********************************************************************
 * Return a RtcDateTime struct with the current date & time values.
 */
RtcDateTime SYS_UTILS::RTCgetDateTime(void)
{
   return Rtc.GetDateTime();
}


/********************************************************************
 * Initialize the ESP32-S3 ADC 
 * @param resol - resolution choices:
 *                9-bit → 0–511 counts
 *                10-bit → 0–1023 counts
 *                11-bit → 0–2047 counts
 *                12-bit → 0–4095 counts (default / maximum)
 * @param atten - attenuation sets the input range:
 *                ADC_0db        0 – 0.8 V
 *                ADC_2_5db      0 – 1.1 V
 *                ADC_6db        0 – 1.5 V
 *                ADC_11db       0 – 3.3 V (default / maximum)
 */
void SYS_UTILS::initADC(uint8_t resol, adc_attenuation_t atten)
{
   analogReadResolution(resol);               // 12-bit
   analogSetAttenuation(atten);         // up to ~3.3V typical
}


/********************************************************************
 * Returns the battery voltage. 
 * @param - avg = number of samples
 * @return - average of all samples converted to (float) volts.
 */
float SYS_UTILS::getBatteryVolts(uint32_t avg)
{
   float v = 0;
   uint32_t i;

   for(i=0; i<avg; i++) {
      // read adc using espressif adc calib "under the hood"
      v += float(analogReadMilliVolts(PIN_BATV_ADC)); 
   }
   // calc using resistor divider (2:1) 
   return (v / (avg * 500.0f));    // convert mv to float volts
}


/********************************************************************
 * Returns the battery charge current in milliamps.
 */
float SYS_UTILS::getBatChgCurrent(uint32_t avg)
{
   float v = 0;
   uint32_t i;
   #define CONV_FACTOR        (11.0 / 12.0)

   for(i=0; i<avg; i++) {
      // read adc using espressif adc calib "under the hood"
      v += float(analogReadMilliVolts(PIN_BATCHG_ADC)); 
   }   
   v = (v / avg) * CONV_FACTOR;   // formula from TP4056 datasheet
   return v;
}


/********************************************************************
 * @brief Return the current battery state of charge 0 - 100%. SOC is 
 * an estimate based on a typical discharge curve for lithium batteries.
 * @param batv - battery voltage in millivolts. If 0, call function
 *       to read battery voltage and convert to millivolts.
 */
uint8_t SYS_UTILS::calcBatSOC(float batv)
{
   uint8_t result;
   uint16_t bv;

   bv = int(batv * 1000);                 // convert to volts to millivolts

   result = 104 - (104 / (1 + pow(1.725 * (bv - MIN_VOLTAGE)/(MAX_VOLTAGE - MIN_VOLTAGE), 5.5)));
	return result >= 100 ? 100 : result;   // result = 0-100%
}


/********************************************************************
 * Return pointer to a system_power_t struct with current values.
 */
system_power_t * SYS_UTILS::getPowerInfo(void)
{
   static system_power_t pwr_info;
   float fl;

   pwr_info.battery_volts = getBatteryVolts();
   pwr_info.charge_current = getBatChgCurrent();
   pwr_info.state_of_charge = calcBatSOC(pwr_info.battery_volts);
   fl = 1.0 - (float(pwr_info.state_of_charge) / 100.0f);
   fl = ((float(BATTERY_CAPACITY) / float(CHARGE_CURRENT)) * fl) * 60.0;

   pwr_info.time_to_charge = int(ceil(fl));
   return &pwr_info;
}


/********************************************************************
*  @brief Print data to the console in HEX format.
*  @param bytes - pointer to data to be displayed
*  @param num_bytes - number of bytes to display. Output is aligned to
*     16 byte segments.
*/
void SYS_UTILS::hexDump(uint8_t *byte_ptr, uint32_t num_bytes)
{
   uint16_t i = 0;
   uint16_t j = 0;
   char ascii_buf[20];
   uint8_t abyte;

   if(num_bytes > 16 && num_bytes % 16 != 0)                // align to 16 bytes
      num_bytes += num_bytes % 16;

   Serial.printf("0x%04X  ", i);          // print buffer index 

   while(i < num_bytes)
   {
      abyte = byte_ptr[i];
      if(abyte < 0x20 || abyte > 0x7E)    // valid ascii char?
         ascii_buf[j] = '.';
      else
         ascii_buf[j] = abyte;

      ascii_buf[++j] = 0x0;               // null term end of string
      Serial.printf("0x%02X ", byte_ptr[i++]);   // print hex value of data
      if(j == 16)
      {
         Serial.print("  ");
         Serial.print(ascii_buf);
         Serial.println("");
         Serial.printf("0x%04X  ", i);       
         j = 0;
      }
   }
   Serial.println("");
}


/********************************************************************
 * @brief Check memory diagnostics
 */
void SYS_UTILS::mem_report(const char* tag)
{
  size_t free_heap = esp_get_free_heap_size();
  size_t min_heap  = esp_get_minimum_free_heap_size();

  size_t free_int  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t free_dma  = heap_caps_get_free_size(MALLOC_CAP_DMA      | MALLOC_CAP_8BIT);
  size_t free_ps   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM   | MALLOC_CAP_8BIT);

  size_t big_int   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t big_dma   = heap_caps_get_largest_free_block(MALLOC_CAP_DMA      | MALLOC_CAP_8BIT);

  Serial.printf("\n[%s]\n", tag);
  Serial.printf(" heap free=%u  min=%u\n", (unsigned)free_heap, (unsigned)min_heap);
  Serial.printf(" internal=%u (largest=%u)\n", (unsigned)free_int, (unsigned)big_int);
  Serial.printf(" dma=%u (largest=%u)\n", (unsigned)free_dma, (unsigned)big_dma);
  Serial.printf(" psram=%u\n", (unsigned)free_ps);
}


/**
 * @brief
 */
void SYS_UTILS::lvgl_mem_report(const char* tag)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("[%s] LVGL free=%u biggest=%u frag=%u%%\n",
                  tag,
                  (unsigned)mon.free_size,
                  (unsigned)mon.free_biggest_size,
                  (unsigned)mon.frag_pct);
}


/********************************************************************
 * @brief Return temperature from clock chip in degrees C
 */
float SYS_UTILS::RTCGetTempC(void)
{
   RtcTemperature tc;
   tc = Rtc.GetTemperature();
   return tc.AsFloatDegC();
}

// ==================================================================
//                     --- TimerManager  Class ---
// ==================================================================

/**
 * @brief Timer Class Functions
 */
TimerManager::TimerManager(void)
{
   for (int i = 0; i < MAX_TIMERS; ++i) {
      timers[i].h        = nullptr;
      timers[i].expiry   = 0;
      timers[i].user_cb  = nullptr;
      timers[i].active   = false;
   }
}


/********************************************************************
 * @brief Start a new timer
 * @param duration_ms - uh huh!
 * @param TimerCallback - pointer to unified callback function.
 */
int TimerManager::startTimer(uint32_t duration_ms, TimerCallback cb) {
    // find a free slot
    int id = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].active) {
            id = i;
            break;
        }
    }
    if (id < 0) return -1;

    TimerSlot &slot = timers[id];

    if (!slot.h) {
        // create a one-shot software timer
        slot.h = xTimerCreate(
            "WTimer",
            pdMS_TO_TICKS(duration_ms),
            pdFALSE,                          // one-shot
            (void*)(intptr_t)id,              // store ID in timer
            timerCallback                     // shared callback
        );
        if (!slot.h) return -1;
    } else {
        // update period if timer is reused
        xTimerChangePeriod(slot.h, pdMS_TO_TICKS(duration_ms), 0);
    }

    slot.expiry  = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
    slot.user_cb = cb;
    slot.active  = true;

    xTimerStart(slot.h, 0);
    return id;
}


/********************************************************************
 * @brief Cancel an active timer. 
 * @param id - handle of the timer to cancel.
 */
void TimerManager::cancelTimer(int id) {
    if (id < 0 || id >= MAX_TIMERS) return;

    TimerSlot &slot = timers[id];
    if (slot.h) xTimerStop(slot.h, 0);
    slot.active  = false;
    slot.user_cb = nullptr;
}


/********************************************************************
 * @brief Return remaing time in milliseconds.
 */
uint32_t TimerManager::remainingMs(int id) {
    if (id < 0 || id >= MAX_TIMERS) return 0;
    
    TimerSlot &slot = timers[id];
    if (!slot.active) return 0;

    TickType_t now = xTaskGetTickCount();
    if (slot.expiry <= now) return 0;

    return (slot.expiry - now) * portTICK_PERIOD_MS;
}


/********************************************************************
 * @brief Return true if timer (id) is active.
 */
bool TimerManager::isActive(int id) {
    if (id < 0 || id >= MAX_TIMERS) return false;
    return timers[id].active;
}


/********************************************************************
 * @brief Timer callback function
 */
void TimerManager::timerCallback(TimerHandle_t xTimer) {
    // retrieve timer slot ID
    int id = (int)(intptr_t)pvTimerGetTimerID(xTimer);
    if (id < 0 || id >= MAX_TIMERS) return;

    TimerSlot &slot = TimerMgr.timers[id];
    slot.active = false;

    if (slot.user_cb) {
        TimerCallback cb = slot.user_cb;
        slot.user_cb = nullptr; // prevent duplicate callbacks
        cb(id);                 // notify
    }
}


