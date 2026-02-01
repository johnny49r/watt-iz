# Watt-IZ Text-to-Speech Demo Project
The goal of this project is to demonstrate the text-to-speech feature of the Watt-IZ Speech 
Enabled Development Board. 

## Hardware Requirements
- Functional Watt-IZ hardware V1.3 or higher with the following working peripherals:
    - Functional 2.8" LCD.
    - Formatted 32GB SD card with ***wattiz_config.json*** file containing a valid google API key.
- Connect a small 4 ohm speaker to the SPKR pins on the PCB. 

## Software Requirements
- A valid google API key to access Text-To-Speech service. To obtain a valid API key see the
**WATT_IZ_API_KEYS.md** file for details. 

## How to Download Demo Firmware
Demo's and apps can be downloaded using the SD card without the need for a development 
system, download cable, etc. Perform the following steps:
1) Turn Watt-IZ power off and remove the SD card.
2) Put the SD card into a PC or suitable phone that can edit SD card files.
3) Copy the 'firmware.bin' file found in the github demo repository to the '/update' folder 
on the SD card. Example: "/update/firmware.bin".
4) Replace the SD card in the Watt-IZ, power up, and follow directions on the pop-up menu.

## Run the Demo
Download the Text-to-Speech demo program and scroll to page 2.
1) After the WiFi has connected to your router the WiFi icon on the upper left of the display 
will be a solid bright white indicating that the device is connected to the internet. If the
WiFi icon does not indicate connection, check your WiFi SSID (router name) and PASSWORD in the
***wattiz_config.json*** file.
2) Press 'Play' to start the Text-To_speech service. The service will play the text paragraph
shown on the display. If this isn't working, check that the API key string in the 
***wattiz_config.json*** file is correct and that you have Text-To-Speech option enabled in your 
google API account.

### Language & Voice Support
A large number of languages and voice accents are supported through the google TTS cloud service. 
See https://docs.cloud.google.com/text-to-speech/docs/list-voices-and-types .

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .

