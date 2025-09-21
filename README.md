# The Watt-iz Speech Enabled Project

## Hardware Description
The Watt-IZ project hardware is packaged on a 100mm x 60mm PCB. See PCB section for details. The
folowing describes the hardware capabilities of each major section.

### Micro Controller Unit (MCU) 
The system is based on the Expressif ESP32-S3 Wroom-1-N16R8 System On Chip (SOC). The variant used in this
design contains 16MB of flash program storage and 8MB of pseudo-static RAM as well as 384Kbytes of SRAM. 
The ESP32 is a dual core 32 bit microprocessor running at 240 Mhz. The -S3 variant also has some DSP functions to
accelerate FFT's, perform audio filtering, and array processing.

The SOC features WiFi and bluetooth connectivity as well as it's own ESP-NOW proprietary communication which does 
not require a router or internet connection to communicate.

The many features of this SOC can be found [here:]https://products.espressif.com/#/product-selector?names=&filter=%7B%22Series%22%3A%5B%22ESP32-S3%22%5D%7D

### Development USB Port
Connection to a development system is done via a micro-USB connector. The board can be powered from this connection
so no other power input is required for development operation.
The board features a CH340-C USB-UART converter chip so debug information can be passed to the development system and
programs compiled in the IDE can be downloaded to the MCU. Download is automatic - no boot button required.

### Power Input
The board is designed to operate from a standard USB-C port. A typical 5V @ 2A AC adapter can be used to power the board.
Additionally the board can be powered from a single Lithium-Ion 3.7V battery. Recommended battery capacity is 2000 mAH.
The battery is connected to the board via a two pin header located next to the USB-C connector. Note the board has 
reverse polarity protection in case the battery is reversed.
A 900 mA charger circuit is provided to charge the battery when the AC adapter is powering the board. Charge status 
LED indicates charging (or full). Power ON/OFF switch is located on the front edge of the board.
The battery voltage and charge current are exposed to the MCU's ADC for applications requiring power monitoring.

High efficiency switching regulators provide precise voltage and maximum battery life.

### Real Time Clock 
The board contains a real-time clock chip which is backed up by a CR2032 coin cell. The batter life is calculated to be 
at least 2 years when no power is applied to the board.
Date and time are kept by the RTC chip as well as alarm functionality. The RTC can be programmed manually or via internet 
(see examples).
The RTC chip also provides substrate temperature (board temperature) in degrees C.

### SD Card Memory
The board has a micro-SD card slot which uses a SDHC or SDXC card. The SD interface in 4-bit MMC for maximum performance. 
Large capacity cards (up to 256 GB) are supported as long as they are formatted with FAT32. The standard card used is an
SDHC 32 GB class 10. 

### 2.8" TFT Touch Screen Display
The hardware supports a 2.8" TFT screen using the ILI9341 graphics controller with SPI interface.
The hardware supports either resistive touch screen or capacitive touch screens. Capacitive touch is more sensitive 
and provides smoother operation but does not work with a stylus. 
Screen brightness can be controlled from 0 to 100% (60 ma backlight current).

### Wake From Deep Sleep
The board features a 'Wake' button which can be used as a general purpose button or as a Wake from deep sleep button.
See examples.

### Audio Hardware 
The Watt-IZ hardware is speech enabled and features a built-in I2S MEMS microphone (ICS-43434) and a high efficiency 3W class-D 
audio amplifier (MAX-98357) used to drive a 4 ohm speaker. 
The microphone is mounted directly to the PCB and provides quality audio for recording or speech transcription. 

### NEOPIXEL Status LED
A WS2812B multicolor status LED provides a full 24 bit color spectrum and variable intensity.

## MIT License
The software examples are free for use, modification, and distribution. See the LICENSE file for details. For commercial use
I only request an acknowledgement of the source. John F Hoeppner, Watt-IZ, Abbycus 2025.
