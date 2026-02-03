# Watt-IZ How to Acquire and Use API Keys
This document shows how to set up a developer account and obtain an API key for 
use with the following cloud speech services:
- Google Speech To Text (STT).
- Google Text To Speech (TTS).
- Google Language Translator.
- OpenAI Chat-GPT.

## Acquire a Google API Key
1) Go to https://console.cloud.google.com and sign in (or create a new) Google account.
2) Click the project selector (top bar) → New Project → give it a name → Create.
3) Make sure your new project is selected.
4) In the left menu, go to APIs & Services → Library.
5) Search for the service you need (e.g., Speech-to-Text, Translate, Maps, etc.).
6) Click the API → Enable.
7) Important: For the Watt-IZ demos, make sure you enable the following services:
    - Speech To Text
    - Text To Speech
	- Translator
8) Go to APIs & Services → Credentials.
9) Click Create Credentials → API Key.
10) Your API key is generated, copy and save it securely.
11) (Recommended) Click Restrict Key:
    - Limit which APIs it can access.
    - Add application restrictions (IP, HTTP referrer, or app type).
    - Save changes.
12) If required by the API:
    - Go to Billing and attach a billing account to your project.

### How to Apply the New Google API Key
#### Method A
1) Power off the Watt-IZ device and remove the SD Card.
2) Insert card into a PC or phone that can edit files from the SD Card.
3) Open the file on the root named 'wattiz_config.json'. If this file doesn't exist, 
use Method B.
4) Find the "google": "api_key": section of the JSON structure and insert the API key
inside the quote marks.
5) Save the file and remove the SD card. Replace the SD card in the device and power on.

#### Method B
1) Power off the Watt-IZ device and remove the SD Card.
2) Insert card into a PC or phone that can edit files from the SD Card.
3) Open the file on the root named 'credentials.h'.
4) Insert API key followintg the line GOOGLE_API_KEY between the quotes.
5) Save the file and remove the SD card. Replace the SD card in the device and power on.
6) Use the 'watt_iz_files' demo to format the SD card and create a new config file.


## OpenAI (Chat GPT) API Key
An openAI API key is required to access Chat GPT and other OpenAI services. Use the following steps 
to acquire a new key:
1) Go to https://platform.openai.com and sign in (or create an account).
2) Click your profile icon (top-right) → View API keys (or go directly to the API Keys page from the dashboard).
3) Click Create new secret key.
4) Give the key a name (optional but recommended).
5) **Copy the key immediately and store it securely - You will not be able to view it again!**
6) Set usage controls (Recommended):
    - Go to Settings → Billing to add a payment method if required.
    - Review Usage Limits to avoid unexpected charges.

## How to Use the openAI API key in Watt-IZ Demo's and Apps
Follow Method A or Method B instructions above but paste the new openAI API key into the
OPENAI_API_KEY text line.
**Never hard-code it into public source code.**

## How to Create a Watt-IZ Configuration File
### Method A
The config file can be created by using the *watt_iz_files* demo program as follows:
    - Download the demo program “watt_iz_files”.
    - Copy your WiFi credentials and cloud API keys to the “credentials.h” file in the project directory.
    - Run the demo and go to the “SD Card Format Utilities” page.
    - If the card hasn’t been formatted or has a format other than FAT32, click ‘FORMAT’.
    - Finally click the ‘CREATE’ button to generate the JSON file and copy credentials.

This config file can be copied to other SD cards using the same API key and WiFi credentials.

### Method B
The config file can also be created by copying the following empty JSON template and pasting it to 
an SD card file named 'wattiz_config.json'. Then edit the file to add WiFi credentials and any API keys.

{
  "version": 2,
  "device": {
    "system_id": "Abbycus Watt-IZ V1.3"
  },
  "wifi": {
    "ssid": "*** Replace with your WiFi SSID",
    "password": "*** Replace with your WiFi password"
  },
  "google": {
    "api_tts_endpoint": "https://texttospeech.googleapis.com/v1/text:synthesize?key=",
    "api_stt_endpoint": "https://speech.googleapis.com/v1/speech:recognize?key=",
    "api_translate_endpoint": "https://translation.googleapis.com/language/translate/v2?key=",
    "api_key": "*** Replace with your google API key string",
    "api_server": "speech.googleapis.com"
  },
  "openai": {
    "api_endpoint": "https://api.openai.com/v1/chat/completions",
    "api_key": "*** Replace with your openAI API key string",
    "chat_version": "gpt-4.1"
  },
  "updated_at": "Fri, Jan 23, 2026 @ 14:00"
}


### Notes: 
1) The 'openai' section 'chat version' lists the desired chat GPT model. There are many models
depending on usage, cost, and utility. Version "gpt-4.1" is low cost and good for most types of
chat requests. Visit https://platform.openai.com/docs/models for more info on models.