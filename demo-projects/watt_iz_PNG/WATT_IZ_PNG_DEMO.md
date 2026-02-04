# Watt-IZ Speech To Text Demo Project
The goal of this project is to demonstrate display of a PNG image file. A file called
**test.png** should be written to the root of the SD card. The **test.png** file is a small 64x64 pixel
image. **Note:** PNG files larger than 64x64 pixels might not render due to limited lvgl graphics memory.

## Hardware Requirements
- Functional Watt-IZ hardware with the following working peripherals:
    - Functional 2.8" LCD.
    - Installed 32GB SD card with wattiz_config.json file containing google API key.

## LVGL Configuration
Open the **'lv_conf.h'** file in the .pio/libdeps/lvgl project folder and edit as shown:
- #define LV_USE_LODEPNG 1      // enable png decoder
- #define LV_MEM_SIZE (128 * 1024U)  // increase drawing memory to 128K
- Enable lvgl file support:
    - #define LV_USE_FS_STDIO 1
    - #define LV_FS_STDIO_LETTER 'S'
    - #define LV_FS_DEFAULT_DRIVER_LETTER 'S'

## How to Download Demo Firmware
Demo's and apps can be downloaded using the SD card. If the Watt-IZ has a demo or app running, the
device will have code necessary to download new firmware. Perform the following:
1) Turn Watt-IZ power off and remove the SD card.
2) Put the SD card into a PC or suitable phone that can edit SD card files.
3) Copy the 'firmware.bin' file in this repository to the '/update' folder on the SD card:
(/update/firmware.bin).
4) Replace the SD card in the Watt-IZ, power up, and follow directions on the pop-up menu.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .
