# Watt-IZ Clcok Demo Project
The goal of this project is to provide working example code showing the basics 
of initializing the graphics engine, displaying True Type Fints (TTF), basics of
the real-time clock chip, and internet time syncronization. 

## Hardware Requirements
- Working Watt-IZ board revision 1.3 or higher.
- Functional 2.8" LCD with either capacitive or resistive touch screen.
- Optional: Functional SD card with WiFi credentials set in the 'wattiz_config.json' file - 
see page 4 description.

## Page 1: Demo Home Screen
Introduction page. Swipe right to view next page.

## Page 2: Clock Display
The clock page displays time and date. The 7 segment TTF characters were converted for use
in the Watt-IZ environment by using the LVGL Font Converter @ https://lvgl.io/tools/fontconverter .
The source code shows examples of declaring and using custom TTF fonts via the LVGL library.

A calendar (LVGL widget) can also be viewed by clicking the small button with the calendar icon.

Date and time can be manually set by clicking the 'Set DateTime button (see next).

## Page 3: Date/Time Manual Setting
Date and time are set manually by clicking on the various dropdown menus to set time, day, month, and 
year, then clicking 'Set Time' button. 

## Page 4: Time/Date Internet Syncronization
If the WiFi SSID and Password have been entered in the SD card configuration file "wattiz_config.json", 
time and date can be automatically set via the internet by setting the Timezone value and clicking the 
'DateTime Sync' button. This will download the UTC time from one of the Network Time Protocol (NTP) 
servers. The 'timezone' value is added to the UTC time to display the correct time in your time zone.
If you have the coin cell battery installed, the correct date/time should be restored after reset or
power off/on.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .


