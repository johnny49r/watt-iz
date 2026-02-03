# Watt-IZ Speech To Text Demo Project
The goal of this example project is to demonstrate Speech-to-Text (STT) feature of the Watt-IZ Speech 
Enabled Development Board. 

This demo utilizes googles Speech-to-Text API to convert voice audio to text. The demo makes 
an audio recording of up to 10 seconds. The recorded audio is saved as a file on the SD card 
named "/rec_test.wav". See **Running the Demo** below.

## Hardware Requirements
- Functional Watt-IZ hardware with the following working peripherals:
    - Functional 2.8" LCD.
    - Installed 32GB SD card with wattiz_config.json file containing google API key.
- Small 4 ohm speaker connected to the SPKR pins. 

## Software Requirements
- Valid google API key to access Speech-To-Text service. To obtain a google API key, see the
**WATT_IZ_API_KEYS.md** file. 

## How to Download Demo Firmware
Demo's and apps can be downloaded using the SD card. If the Watt-IZ has a demo or app running, the
device will have code necessary to download new firmware. Perform the following:
1) Turn Watt-IZ power off and remove the SD card.
2) Put the SD card into a PC or suitable phone that can edit SD card files.
3) Copy the 'firmware.bin' file in this repository to the '/update' folder on the SD card:
(/update/firmware.bin).
5) Replace the SD card in the Watt-IZ, power up, and follow directions on the pop-up menu.

## Running the Demo
1) After the WiFi has connected to your router the WiFi icon on the upper left of the display 
will be a solid bright white indicating that the device is connected to the internet. If the
WiFi icon does not indicate connection, check your WiFi SSID (router name) and PASSWORD in the
***wattiz_config.json*** file.
2) Press the 'Speak' button and say a few words or a sentence. The 'Speak' button will now show
'Stop'. The demo takes a 10 second audio recording which starts after you begin speaking. You 
can end speaking by pressing 'Stop' or simply stop speaking for approximately 2 seconds. The
recording will always end a maximum of 10 seconds after it starts.
Voice audio is simultaneously sent to googles STT service and a text response should appear shortly 
in the text box on the display. If this isn't working, check that the API key string in the 
***wattiz_config.json*** file is correct and that you have the Speech-to-Text option enabled 
in your google API account.

A replay of your original voice is also presented to validate the text response.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .
