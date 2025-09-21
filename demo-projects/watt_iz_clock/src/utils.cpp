/**
 * utils.cpp
 */
#include "utils.h"

SYS_UTILS sys_utils;

RtcDS3231<TwoWire> Rtc(Wire);             // Using this RTC lib cuz Adafruit lib didn't work!

SYS_UTILS::SYS_UTILS(void)
{
   // empty constructor
}


/********************************************************************
 * Save new date/time values to the RTC chip DS3231
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
 * Intialize RTC chip (DS3231). Returns true if successful.
 */
bool SYS_UTILS::RTCInit(uint8_t sda, uint8_t sck)
{
   Rtc.Begin(sda, sck);
   Rtc.GetTemperature();
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
 * Return temperature from RTC chip in degrees C
 */
float SYS_UTILS::RTCGetTempC()
{
   float tempc;
   RtcTemperature rtc_temp;

   if(RTCIsRunning()) {
      rtc_temp = Rtc.GetTemperature();
      tempc = rtc_temp.AsFloatDegC();
   } else 
      tempc = 0.0;
   return tempc;
}


/********************************************************************
 * Initialize the ESP32-S3 ADC 
 * @param resol - resolution choices:
 *                9-bit → 0–511 counts
 *                10-bit → 0–1023 counts
 *                11-bit → 0–2047 counts
 *                12-bit → 0–4095 counts (default / maximum)
 * @param atten - attenuation sets the input range:
 *                ADC_0db        0 – ≈0.8 V
 *                ADC_2_5db      0 – ≈1.1 V
 *                ADC_6db        0 – ≈1.5 V
 *                ADC_11db       0 – ≈3.3 V (default / maximum)
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
   // calc using resistor divider (2:1) and mv->volts (1000) = 500.0
   return (v / (avg * 500.0f));    // convert mv to float volts
}


/********************************************************************
 * Returns the external voltage. 
 */
float SYS_UTILS::getExtPowerVolts(uint32_t avg)
{
   float v = 0;
   uint32_t i;

   for(i=0; i<avg; i++) {
      // read adc using espressif adc calib "under the hood"
      v += float(analogReadMilliVolts(PIN_EXTPWR_ADC)); 
   }
   // calc using resistor divider (2:1) and mv->volts (1000) = 500.0
   return (v / (avg * 500.0f));    // convert mv to float volts
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
   pwr_info.ext_power_volts = getExtPowerVolts();
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

   if(num_bytes % 16 != 0)   // align to 16 bytes
      num_bytes += num_bytes % 16;

   Serial.printf("0x%04X  ", i);    

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