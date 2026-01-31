/**
 * @brief gdriver.h - Driver code for LCD panel and Touch Screen
 */
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>                  // low level graphics driver
#include "config.h"
#include "ArduinoNvs.h"                   // Non volatile storage - like EEPROM

// Touchpoint struct
typedef struct {
   int16_t x;
   int16_t y;
} touch_point_t ;

// Touch calibration data structure
typedef struct {
   uint16_t min_x;
   uint16_t max_x;
   uint16_t min_y;
   uint16_t max_y;
} calib_data_t ;

// Function prototypes
uint8_t readFT6336(touch_point_t* points, uint8_t maxPoints, uint8_t scrn_rotate);
uint8_t readTouchScreen(touch_point_t* points, uint8_t scrn_rotate);

#define FT6336_ADDR             0x38   

// ---------- LovyanGFX setup (SPI + ILI9341) ----------
class LGFX : public lgfx::LGFX_Device {
   lgfx::Bus_SPI _bus;               // SPI bus
   lgfx::Panel_ILI9341 _panel;       // panel driver (change if your controller differs)
#if defined(USE_RESISTIVE_TOUCH_SCREEN)  
   lgfx::Touch_XPT2046 _touch;       // Resistive touch controller (XPT2046/ADS7843)
#endif

public:
   LGFX() {
   { // Bus config
      auto cfg = _bus.config();
      cfg.spi_host   = SPI3_HOST;      // VSPI on S3
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;       // SPI write freq for ILI9341 ctlr
      cfg.freq_read  = 20000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = 1;             // -1=auto, 1 or 2 are fine
      cfg.pin_sclk   = PIN_SPI_CLK;
      cfg.pin_mosi   = PIN_SPI_MOSI;
      cfg.pin_miso   = PIN_SPI_MISO;   // not used typically
      cfg.pin_dc     = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
   }
   { // Panel config
      auto cfg = _panel.config();
      cfg.pin_cs        = PIN_LCD_CS;
      cfg.pin_rst       = PIN_RST_NA;
      cfg.pin_busy      = -1;             // not used
      cfg.panel_width   = 240;            // portrait values
      cfg.panel_height  = 320;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.offset_rotation = 0;
      cfg.readable      = false;
#if defined(USE_CAPACITIVE_TOUCH_SCREEN) 
      cfg.invert        = true;
#elif defined(USE_RESISTIVE_TOUCH_SCREEN)  
      cfg.invert        = false;          
#endif          
      cfg.rgb_order     = false;
      cfg.dlen_16bit    = false;
      cfg.bus_shared    = true;
      _panel.config(cfg);
   }
#if defined(USE_RESISTIVE_TOUCH_SCREEN)  
   {  // Touch Config
      auto tc = _touch.config();

      // If sharing the LCD SPI bus:
      tc.spi_host   = SPI3_HOST;          // same host as panel
      tc.freq       = 2500000;            // 2.5 MHz is reliable for XPT2046
      tc.pin_sclk   = PIN_SPI_CLK;        // same clk as tft
      tc.pin_mosi   = PIN_SPI_MOSI;
      tc.pin_miso   = PIN_SPI_MISO;
      tc.bus_shared = true;

      tc.pin_cs  = PIN_LCD_TCS;           // Touch CS
      tc.pin_int = PIN_LCD_TIRQ;          // Touch IRQ (optional, -1 if not wired)

      // Try reading resistive calibration values from NVS storage.
      // Use calibration defaults if values haven't been written yet.
      tc.x_min = 300;  tc.x_max = 3800;   // load defaults
      tc.y_min = 300;  tc.y_max = 3800;      
      calib_data_t calib_data;            // minx, maxx, miny, maxy
      uint16_t blobLength;
      if(NVS.begin()) {   
         blobLength = NVS.getBlobSize("calib_data");  // get the stored struct length
         if(NVS.getBlob("calib_data", (uint8_t *)&calib_data, blobLength)) {
            tc.x_min = calib_data.min_x;  // overwrite calib defaults 
            tc.x_max = calib_data.max_x;
            tc.y_min = calib_data.min_y;
            tc.y_max = calib_data.max_y;
         } 
      }

      // Physical screen size used to map to display coordinates:
      tc.offset_rotation = 4;  // If your touch appears rotated, tweak this (0–7)

      _touch.config(tc);
      _panel.setTouch(&_touch);  
   }
#endif
    setPanel(&_panel);
  }
};

extern LGFX gfx;