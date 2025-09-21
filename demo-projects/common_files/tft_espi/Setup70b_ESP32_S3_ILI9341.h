// Setup for the ESP32 S3 with ILI9341 display
// Note SPI DMA with ESP32 S3 is not currently supported
#define USER_SETUP_ID 70
// LCD graphics driver type
#define ILI9341_DRIVER

                              // Typical board default pins - change to match your board
#define TFT_CS       47       //     10 or 34 (FSPI CS0) 
#define TFT_MOSI     40       //     11 or 35 (FSPI D)
#define TFT_SCLK     38       //     12 or 36 (FSPI CLK)
#define TFT_MISO     41       //     13 or 37 (FSPI Q)

// Use pins in range 0-31
#define TFT_DC      48
#define TFT_RST     -1        // LCD reset tied to MCU reset
#define TOUCH_CS    39        // for resistive touch screens

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

// FSPI (or VSPI) port (SPI2) used unless following defined. HSPI port is (SPI3) on S3.
//#define USE_HSPI_PORT

#define SPI_FREQUENCY  27000000
//#define SPI_FREQUENCY  40000000     // Maximum for ILI9341

#define SPI_READ_FREQUENCY  6000000 // 6 MHz is the maximum SPI read speed for the ST7789V

#define SPI_TOUCH_FREQUENCY 2500000 // for resistive touch 

// Enable for some displays - trial & error.
#define TFT_INVERSION_ON 
// #define TFT_INVERSION_OFF