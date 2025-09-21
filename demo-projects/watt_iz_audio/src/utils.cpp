/**
 * utils.cpp
 */
#include "utils.h"

SYS_UTILS::SYS_UTILS(void)
{
   // empty constructor
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