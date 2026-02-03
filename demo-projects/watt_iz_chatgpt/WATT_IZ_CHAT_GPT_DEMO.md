# Watt-IZ Speech To Text Demo Project
The goal of this project is to demonstrate communication with openAI Chat GPT using 
the Watt-IZ Speech Enabled Development Board. A text message will be sent to Chat GPT and
the response text from the chat service will be shown in a text box.

With the Watt-IZ speech enabled tools you can easily add a voice-in frontend and voice-out 
backend to the chat GPT service. A functional example of this can be found in the Watt-IZ app.

## Hardware Requirements
- Functional Watt-IZ hardware with the following working peripherals:
    - Functional 2.8" LCD.
    - Installed 32GB SD card with wattiz_config.json file containing google API key.
- Small 4 ohm speaker connected to the SPKR pins. 

## Software Requirements
- Valid openAI API key to access Chat GPT services. To obtain a valid API key see the
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
2) Press 'Ask Me Anything'. A keyboard popup will appear allowing a text message to be entered.
After entering the text message and clicking the Check key on the bottom right of the keyboard, the
text will be sent to openAI's Chat GPT service. Response text will appear shortly in the 
Response text box on the display. If this isn't working, check that the API key string in the 
wattiz_config.json file is correct and that you have Speech-to-Text option enabled in your 
google API account.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .
