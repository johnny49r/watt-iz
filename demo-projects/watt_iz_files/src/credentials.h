#include <Arduino.h>

/**
 * @brief This file contains PRIVATE credencial information for wifi access, 
 * google speech service, and openAI chat GPT access.
 */

 #define WIFI_SSID          "your_wifi_ssid"            // PRIVATE!
 #define WIFI_PASSWORD      "your_wifi_password"        // PRIVATE!
 #define GOOGLE_SERVER      "speech.googleapis.com"
 #define GOOGLE_ENDPOINT    "https://texttospeech.googleapis.com/v1/text:synthesize?key="
 #define GOOGLE_API_KEY     "your_google_api_key"       // PRIVATE!
 #define OPENAI_ENDPOINT    "https://api.openai.com/v1/chat/completions"
 #define OPENAI_API_KEY     "your_openai_api_key"       // PRIVATE!
 #define OPENAI_CHAT_MODEL  "gpt-4.1-mini"
