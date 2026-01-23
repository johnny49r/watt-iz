# Watt-IZ Text to Speech Demo Project
The goal of this project is to demonstrate the text-to-speech feature of the Watt-IZ Speech 
Enabled Development Board. 

## Hardware Requirements
- Functional Watt-IZ hardware with the following working peripherals:
    - Functional 2.8" LCD.
    - Installed 32GB SD card with wattiz_config.json file containing a valid google API key.
- Small 4 ohm speaker connected to the SPKR pins. 

## Software Requirements
- Valid google API key to access Text-To-Speech service. To obtain a valid API key see the
**WATT_IZ_API_KEYS.md** file.

## How to Download Demo Firmware
Demo's and apps can be downloaded using the SD card. If the Watt-IZ has a demo or app running, the
device will have code necessary to download new firmware. Perform the following:
1) Turn Watt-IZ power off and remove the SD card.
2) Put the SD card into a PC or suitable phone that can edit SD card files.
3) Copy the 'firmware.bin' file in this repository to the '/update' folder on the SD card:
(/update/firmware.bin).
5) Replace the SD card in the Watt-IZ, power up, and follow directions on the pop-up menu.

## Instructions
1) After the WiFi has connected to your router the WiFi icon on the upper left of the display 
will be a solid bright white indicating that the device is connected to the internet. If the
WiFi icon does not indicate connection, check your WiFi SSID (router name) and PASSWORD in the
wattiz_config.json file.
2) Press 'Play' to start the Text-To_speech service. The service will play the text paragraph
shown on the display. If this isn't working, check that the API key string in the 
wattiz_config.json file is correct and that you have Text-To-Speech option enabled in your 
google API account.

This code can be replicated to build more complex speech functions. 
