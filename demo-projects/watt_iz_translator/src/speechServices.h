#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "audio.h"
#include "gui.h"
#include "sd_lvgl_fs.h"


/**
 * @brief Language definitions for speech services.
 * See link for language codes & voice name options:
 * https://www.science.co.il/language/Locale-codes.php
 */
#define STT_LANGUAGE                      "en-US"  
#define TTS_LANGUAGE                      "en-US"
#define TTS_VOICE                         "en-US-Wavenet-G" 
#define TTS_AUDIO_ENCODING                "LINEAR16"

#define STT_CHUNK_SIZE                    1536     // chunk size before base64 encoding
#define STT_ENC_CHUNK_SIZE                2048     // chunk size after base64 encoding (4/3)

#define MAX_TTS_STRLEN                    1024
#define TTS_CHUNK_SIZE                    4096  // this should be powers of 2
#define TTS_DEC_FILE_SIZE                 3072  // TTS_CHUNK_SIZE * 0.75
#define WAV_HDR_SIZE                      44

#define SYS_CRED_FILENAME                 "/wattiz_config.json"

//###################################################################
// ENABLE SPEECH SERVICES - Disable services not using to save a small
// amount of flash & SRAM.
//###################################################################
#define USE_CHAT_GPT_SERVICE              1
#define USE_SPEECH_TO_TEXT_SERVICE        1
#define USE_TEXT_TO_SPEECH_SERVICE        1
#define USE_LANGUAGE_TRANSLATE_SERVICE    1


#if defined (USE_LANGUAGE_TRANSLATE_SERVICE)
//###################################################################
// LANGUAGE TRANSLATE CLASS - Translate text from one language into 
// another. 
//###################################################################
class LANGUAGE_TRANSLATE {
   public:
      LANGUAGE_TRANSLATE();               // constructor
      ~LANGUAGE_TRANSLATE();              // destructor
      bool init(void);                    // load translate service credentials   
      String translateLanguage(const char *input_text, char *lang_from, char *lang_to);
      
   private:
      // credentials for google translate service
      String langApiServer;               
      String langApiKey;                  // common API key - same as STT & TTS 
      String langApiEndPoint;      
};

#endif


#if defined (USE_TEXT_TO_SPEECH_SERVICE)
//###################################################################
// TEXT TO SPEECH CLASS - Transcribe text into speech. 
//###################################################################
class TEXT_TO_SPEECH {
   public:
      TEXT_TO_SPEECH();                   // constructor
      ~TEXT_TO_SPEECH();                  // destructor
      bool init(void);                    // load tts credentials
      bool transcribeTextToSpeech(String text, bool play_audio, const char *filepath, 
            int16_t volume, char *lang_code, char *voice_name, float speak_rate=1.0);     

   private:
      String ttsApiServer;
      String ttsApiKey;                   // common API key
      String ttsApiEndPoint;
};
#endif



#if defined(USE_SPEECH_TO_TEXT_SERVICE)
//###################################################################
// SPEECH TO TEXT CLASS - Transcribe audio speech into text.
//###################################################################
class SPEECH_TO_TEXT {
   public:
      SPEECH_TO_TEXT();                   // constructor
      ~SPEECH_TO_TEXT();                  // destructor
      bool init(void);                    // load stt api credentials
      bool transcribeSpeechToText(const char *filepath, char *lang_code);  // convert audio to text
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
//###################################################################
// CHAT GPT CLASS - Make a text request of openAI Chat GPT service.
// Note: Call initChatGPT() prior to calls to chatRequest(). 
//###################################################################
class CHAT_GPT {
   public:
      CHAT_GPT();                         // constructor
      ~CHAT_GPT();                        // destructor
      bool initChatGPT(void);             // load chat GPT credentials from SD card
      String chatRequest(const char *req_text, bool enab_tts, uint16_t volume);  // volume used if TTS is enabled

   private:
      String ChatGPTApiKey;
      String ChatGPTEndPoint;      
      String ChatGPTVersion;
      String sendToChatGPT(const String& message);
      String makeHttpRequest(const String& payload);
      String sanitizeString(const String& input);   
};
#endif


/**
 * @brief Expose the class instanciation names for various speech services.
 * These are instanciated in speechServices.cpp
 */
#if defined (USE_CHAT_GPT_SERVICE)
   extern CHAT_GPT chatGPT;
#endif

#if defined (USE_SPEECH_TO_TEXT_SERVICE)
   extern SPEECH_TO_TEXT speechToText;
#endif

#if defined (USE_TEXT_TO_SPEECH_SERVICE)
   extern TEXT_TO_SPEECH textToSpeech;
#endif

#if defined (USE_LANGUAGE_TRANSLATE_SERVICE)
   extern LANGUAGE_TRANSLATE language_xlate;
#endif