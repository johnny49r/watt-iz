# The Watt-iz Project

## Hardware Description
The Watt-IZ project hardware is packaged on a 100mm x 60mm 1.6mm PCB. See PCB section for details. The
folowing describes the hardware capabilities.

### Micro Controller Unit (MCU) 
The system is based on the Expressif ESP32-S3 Wroom-1-N16R8 System On Chip (SOC). The variant used in this
design contains 16MB of flash program storage and 8MB of pseudo-static RAM as well as 384Kbytes of SRAM. 
The many features of this SOC can be found [here:]https://products.espressif.com/#/product-selector?names=&filter=%7B%22Series%22%3A%5B%22ESP32-S3%22%5D%7D

The SOC has WiFi and bluetooth connectivity as well as it's own ESP-NOW communication which does not require a 
router or internet connection to operate.

### Development USB Port
Connection to a development system is done via a micro-USB connector. The board can be powered from this connection
so no other power input is required for normal operation.
The board features a CH340-C USB-UART converter chip so debug information can be passed to the development system and
programs compiled in the IDE can be downloaded to the MCU.
Automatic downloading does not require the use of RESET & BOOT buttons.

### Power Input
The board is designed to operate from a standard USB-C port. A typical 5V @ 2A AC adapter can be used to power the board.
Additionally the board can be powered from a single Lithium-Ion 3.7V battery. The battery is connected to the board via
a two pin header located next to the USB-C connector. Note the board has reverse polarity protection in case the battery 
is reversed.
A 900 mA charger circuit is provided to charge the battery when the AC adapter is powering the board. LED charge status 
LED indicates charging (or full). Power ON/OFF switch is on the front edge of the board.
The battery voltage and charge current are exposed to the MCU for applications requiring power monitoring.

High efficiency switching regulators provided precise voltage and best battery life.

### Real Time Clock 
The board contains a real-time clock chip which is backed up by a CR2032 coin cell. The batter life is calculated to be 
at least 2 years when no power is applied to the board.
Date and time are kept by the RTC chip as well as alarm functionality. The RTC can be programmed manually or via internet 
(see examples).

### SD Card Memory
The board has a SD card slot which uses a SDHC or SDXC card. The SD interface in 4-bit MMC for maximum performance. 
Large capacity cards (up to 256 GB) are supported as long as they are formatted with FAT32. Standard cards used are SDHC 
32 GB class 10.

### 2.8" TFT Touch Screen Display
The hardware supports common 2.8" TFT screens with the ILI9341 controller with SPI interface.
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
A NEOPIXEL WS2812B provides a full 24 bit color spectrum. 

## MIT License
The software examples are free for use, modification, and distribution. See the LICENSE.MD for details.
