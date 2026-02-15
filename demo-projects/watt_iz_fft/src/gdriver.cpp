/**
 * @brief gdriver.cpp
 */
#include "gdriver.h"

LGFX gfx;

/********************************************************************
 * @brief Conflated Read Touch Screen. 
 */
uint8_t readTouchScreen(touch_point_t* points, uint8_t scrn_rotate)
{
#if defined(USE_CAPACITIVE_TOUCH_SCREEN)   
   uint8_t touches = readFT6336(points, 2, scrn_rotate);
   return (touches > 0);
#elif defined(USE_RESISTIVE_TOUCH_SCREEN)
   int16_t touch_x, touch_y;
   uint8_t touch_count = gfx.getTouch(&touch_x, &touch_y);
   if(touch_count > 0) {
      points[0].x = touch_x;
      points[0].y = touch_y;
   }
   return touch_count;                    // touched if count > 0
#endif
}


/********************************************************************
 * @brief Read coords from capacitive touch controller (FT6336).
 * @note: This controller operates over I2C bus.
 */
uint8_t readFT6336(touch_point_t *points, uint8_t maxPoints, uint8_t scrn_rotate) 
{
#define MAX_POINTS   13
   uint16_t px, py;

   Wire.beginTransmission(FT6336_ADDR);
   Wire.write(0x02); // Start at Touch Points register
   if (Wire.endTransmission(false) != 0) return 0;

   // Request 1 byte for count + 12 bytes for 2 points
   Wire.requestFrom(FT6336_ADDR, MAX_POINTS);
   if (Wire.available() < 7) return 0;    // must have at least 7 bytes to return

   uint8_t touches = Wire.read(); // Number of active touches (max 2)
   if(touches > maxPoints) touches = maxPoints;

   for (uint8_t i = 0; i < touches; i++) {
      uint8_t xh = Wire.read();  // XH
      uint8_t xl = Wire.read();  // XL
      uint8_t yh = Wire.read();  // YH
      uint8_t yl = Wire.read();  // YL
      px = ((xh & 0x0F) << 8) | xl;
      py = ((yh & 0x0F) << 8) | yl;
      Wire.read();               // Weight (skip)
      Wire.read();               // Area (skip)

      // Correct X/Y coordinates depending on screen rotation
      switch(scrn_rotate) {
         case 0:
            points[i].x = px;
            points[i].y = py;
            break;

         case 1:
            points[i].y = SCREEN_HEIGHT - px;
            points[i].x = py;
            break;

         case 2:
            points[i].x = SCREEN_WIDTH - px;
            points[i].y = SCREEN_HEIGHT - py;
            break;
         
         case 3:
            points[i].x = SCREEN_WIDTH - py;         
            points[i].y = px;      
            break;
      }
   }
   return touches;
}
