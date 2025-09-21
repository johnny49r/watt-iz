/**
 * utils.h
 */
#pragma once

#include <Arduino.h>

class SYS_UTILS 
{
   public:
      SYS_UTILS(void);                    // constructor
      void hexDump(uint8_t *byte_ptr, uint32_t num_bytes);

   private:

};


extern SYS_UTILS sys_utils;