#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "sdcard.h"
#include "audio.h"
#include "gui.h"


// =================== STRING DEFINITIONS =================
#define MAX_TTS_STRLEN              1024
#define TTS_CHUNK_SIZE              4096  // this should be powers of 2
#define TTS_DEC_FILE_SIZE           3072  // TTS_CHUNK_SIZE * 0.75
#define WAV_HDR_SIZE                44

/**
 * @brief Language definitions for speech services - see here for language codes & options:
 * https://www.science.co.il/language/Locale-codes.php
 */
#define STT_LANGUAGE                "en-US"  
#define TTS_LANGUAGE                "en-US"
#define TTS_VOICE                   "en-US-Wavenet-G" 
#define TTS_AUDIO_ENCODING          "LINEAR16"

#define STT_CHUNK_SIZE              1500
#define STT_ENC_CHUNK_SIZE          2000     // chunk size after base64 encoding (4/3)


#if defined (USE_TEXT_TO_SPEECH_SERVICE)
/********************************************************************
 * @brief Transcribe text into speech. The audio 
 */
class TEXT_TO_SPEECH {
   public:
      TEXT_TO_SPEECH();                   // constructor
      ~TEXT_TO_SPEECH();                  // destructor
      bool init(void);                    // load tts credentials
      bool transcribeTextToSpeech(String text, bool play_audio, const char *filepath, int16_t volume);      

   private:
      String ttsApiServer;
      String ttsApiKey;
      String ttsApiEndPoint;
};
#endif

#if defined(USE_SPEECH_TO_TEXT_SERVICE)
/********************************************************************
 * @brief Transcribe audio speech into text.
 */
class SPEECH_TO_TEXT {
   public:
      SPEECH_TO_TEXT();                   // constructor
      ~SPEECH_TO_TEXT();                  // destructor
      bool init(void);                    // load stt api credentials
      bool transcribeSpeechToText(const char *filepath);  // convert audio file to text
      void setClientTimeout(uint32_t timeout_ms);  // set I/O timeout in milliseconds
      uint32_t getClientTimeout(void);    // returns the I/O timeout in milliseconds
      bool clientKeepAlive(void);      
      bool clientReconnect(void);
      bool isClientConnected(void);       // try to reconnect sttClient after disconnect

      String sttResponse;                 // text response from transcribeSpeechToText
      String sttConfidence;               // confidence of transcribe

   private:
      WiFiClientSecure sttClient;
      String sttApiKey;
      String sttApiServer;
      String sttApiEndPoint;
};
#endif

#if defined(USE_CHAT_GPT_SERVICE)
/********************************************************************
 * @brief ChatGPT Class opens access to the OpenAI ChatGPT services.
 * @note: Call initChatGPT() prior to calls to chatRequest(). 
 */
class CHAT_GPT {
   public:
      CHAT_GPT();                         // constructor
      ~CHAT_GPT();                        // destructor
      bool initChatGPT(void);             // load chat GPT credentials from SD card
      bool chatRequest(const char *req_text, bool enab_tts, uint16_t volume);  // volume used if TTS is enabled

   private:
      String ChatGPTApiKey;
      String ChatGPTEndPoint;      
      String ChatGPTVersion;
      String sendToChatGPT(const String& message);
      String makeHttpRequest(const String& payload);
      String sanitizeString(const String& input);   
};
#endif


#if defined (USE_CHAT_GPT_SERVICE)
   extern CHAT_GPT chatGPT;
#endif

#if defined (USE_SPEECH_TO_TEXT_SERVICE)
   extern SPEECH_TO_TEXT speechToText;
#endif

#if defined (USE_TEXT_TO_SPEECH_SERVICE)
   extern TEXT_TO_SPEECH textToSpeech;
#endif
