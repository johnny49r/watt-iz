/**
 * utils.h
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include "Wire.h"                         // I2C library
#include <RtcDS3231.h>

typedef struct {
   float battery_volts;
   float ext_power_volts;
   uint8_t state_of_charge;
   uint16_t time_to_charge;
} system_power_t ;

// Battery definition
#define CHARGE_CURRENT        900.0          // const charge current in milliamps (float)
#define BATTERY_CAPACITY      2000.0         // mAh (float)
#define MIN_VOLTAGE           2800           // == 0%
#define MAX_VOLTAGE           4200           // == 100%


class SYS_UTILS 
{
   public:
      SYS_UTILS(void);                    // constructor
      void hexDump(uint8_t *byte_ptr, uint32_t num_bytes);

      // wrapper for the Makuna RTC@ DS3231 library
      bool RTCInit(uint8_t sda, uint8_t sck);
      bool RTCIsRunning(void);
      void RTCsetNewDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
      RtcDateTime RTCgetDateTime(void);     
      float RTCGetTempC();


      // Power functions
      void initADC(uint8_t resol=12, adc_attenuation_t atten=ADC_11db);
      float getBatteryVolts(uint32_t avg=8);
      float getExtPowerVolts(uint32_t avg=8);
      uint8_t calcBatSOC(float batv);     // estimated battery SOC, 0-100%
      system_power_t * getPowerInfo(void);


   private:

};

extern SYS_UTILS sys_utils;