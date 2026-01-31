/**
 * utils.h
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include <RtcDS3231.h>
#include "Wire.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "src/stdlib/lv_mem.h"

#define MAX_WIFI_CRED_CHARS      30
#define MAX_NET_NAME_CHARS       20

// ================= SYSTEM SETTINGS STRUCT ===============
typedef struct {
   bool settings_changed;                 // if true, save info to NV storage
   uint8_t brightness;                    // LCD backlight brightness 0 - 255
   char device_name[MAX_NET_NAME_CHARS];  // network name string
   char wifi_ssid[MAX_WIFI_CRED_CHARS];   // wifi router name
   char wifi_password[MAX_WIFI_CRED_CHARS];  // wifi router password
   uint8_t screen_rotation;               // landscape mode, values can be 1 or 3
   struct tm datetime;                    // current date/time
   int8_t timezone;                       // number of hours offset from UTC (-12 to 14)
   bool time_format24;                    // true if using 24 hour time
   int16_t speaker_volume;                // speech to text volume 0-100%
   int16_t microphone_volume;
} sys_settings_t;

// Power structure
typedef struct {
   float battery_volts;
   float charge_current;
   uint8_t state_of_charge;
   uint16_t time_to_charge;
} system_power_t ;

// LCD backlight control params
#define BKLT_CHANNEL          0           // Ledc timer channel - used for backlight dimming
#define BKLT_FREQ             600         // pwm period frequency in HZ
#define BKLT_RESOLUTION       8           // timer resolution - 8 bits

// Battery definition
#define CHARGE_CURRENT        900.0       // const charge current in milliamps (float)
#define BATTERY_CAPACITY      2000.0      // mAh (float)
#define MIN_VOLTAGE           2800        // == 0%
#define MAX_VOLTAGE           4200        // == 100%

#define NVS_KEY               0xDEADBEEF00000001 // change this if sys_settings_t has changed

/**
 * @brief General system utility class
 */
class SYS_UTILS 
{
   public:
      SYS_UTILS() = default;              // constructor / destructor = default
      ~SYS_UTILS() = default;

      sys_settings_t SystemSettings;      // public system setting struct 
      
      // LCD backlight control
      void initBacklight(void);      
      void setBrightness(uint8_t brightness);   // set backlight brightness 0-100%

      // Time keeper - wrapper for the Makuna RTC@ DS3231 library
      bool RTCInit(uint8_t sda, uint8_t sck);
      bool RTCIsRunning(void);
      void RTCsetNewDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
      RtcDateTime RTCgetDateTime(void);
      String makeTzString(int8_t UTC_offset);
      float RTCGetTempC();

      // Power functions
      void initADC(uint8_t resol=12, adc_attenuation_t atten=ADC_11db);
      float getBatteryVolts(uint32_t avg=8);
      float getBatChgCurrent(uint32_t avg=8);
      uint8_t calcBatSOC(float batv);     // estimated battery SOC, 0-100%
      system_power_t * getPowerInfo(void); // get all power info, return ptr to struct

      // Functions for Non-Volatile system settings
      bool initNVS(void);    
      bool saveSettingsNVS(bool reboot);  
      void setVolume(uint8_t volume);
      uint8_t getVolume(void);
      char * getDeviceName(void);         // return ptr to device name
      void setDeviceName(const char *dev_name);
      char * getWifiSSID(void);
      void setWifiSSID(const char *ssid);
      char * getWifiPassword(void);
      void setWifiPassword(const char *password);

      // Diagnostic functions
      void hexDump(uint8_t *byte_ptr, uint32_t num_bytes);      
      void mem_report(const char* tag);
      void lvgl_mem_report(const char* tag);
      void printTaskHighWaterMark(TaskHandle_t h_task);  // task utility to check minimum stack size

   private:
      bool NVS_OK = false;                // true if NVS init was OK        

};

/**
 * @brief Timer Manager Class
 */
typedef void (*TimerCallback)(int id);
#define MAX_TIMERS            5

class TimerManager {
   public:
      // Constructor 
      TimerManager(void);   
      // Destructor = defaulted           
      ~TimerManager() = default;
  
      // Create/start a timer. Returns ID 0..MAX_TIMERS-1 or -1 if none available.
      int startTimer(uint32_t duration_ms, TimerCallback cb);

      // Cancel timer by ID
      void cancelTimer(int id);

      // Check remaining time in milliseconds
      uint32_t remainingMs(int id);

      // Is a timer currently active?
      bool isActive(int id);

   private:
      // Slot for each timer
      struct TimerSlot {
         TimerHandle_t h;
         TickType_t    expiry;
         TimerCallback  user_cb;
         bool          active;
      };

      TimerSlot timers[MAX_TIMERS];

      // Static FreeRTOS callback → routes to real object using global instance
      static void timerCallback(TimerHandle_t xTimer);
};


extern SYS_UTILS sys_utils;

// Global instance - required so static callback can find our object
extern TimerManager TimerMgr;