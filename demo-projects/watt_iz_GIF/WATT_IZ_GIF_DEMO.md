# Watt-IZ Speech To Text Demo Project
The goal of this project is to demonstrate display of a GIF animation file. A file called
**sample_200.gif** should be written to the root of the SD card. The **sample_200.gif** file is a small 200x200 pixel image size. 
**Note:** Larger GIF files require more memory so files larger than 200x200 pixels may not render due to limited lvgl graphics memory. 

## Hardware Requirements
- Functional Watt-IZ hardware with the following working peripherals:
    - Functional 2.8" LCD.
    - Installed 32GB SD card with wattiz_config.json file containing google API key.

## LVGL Configuration
These configuration directions assume you have installed this project in the Visual Studio Code / PlatformIO 
development environment. 
When you first install a new project in vscode, all dependent libraries will be installed. When LVGL is first 
installed the file **lv_conf.h** will be missing. Find the **lv_conf.h.copy** in the project include folder 
and copy to the lvgl directory in .pio/libdeps/lvgl. Remove the 'copy' suffix (result = lv_conf.h).

Open the **'lv_conf.h'** file in the .pio/libdeps/lvgl project folder and edit as shown:
- #define LV_USE_GIF 1      // enable GIF decoder
- #define LV_MEM_SIZE (240 * 1024U)  // support GIF 200x200
- Enable lvgl file support:
    - #define LV_USE_FS_STDIO 1
    - #define LV_FS_STDIO_LETTER 'S'
    - #define LV_FS_DEFAULT_DRIVER_LETTER 'S'

## How to Download Demo Firmware From SD Card
Demo's and apps can be downloaded using the SD card. If the Watt-IZ has a demo or app running, the
device will have code necessary to download new firmware. Perform the following:
1) Turn Watt-IZ power off and remove the SD card.
2) Put the SD card into a PC or suitable phone that can edit SD card files.
3) Copy the 'firmware.bin' file in this repository to the '/update' folder on the SD card:
(/update/firmware.bin).
4) Replace the SD card in the Watt-IZ, power up, and follow directions on the pop-up menu.

## Full Project
The complete VSCode projects with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .
