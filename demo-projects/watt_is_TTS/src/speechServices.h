#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "audio.h"
#include "gui.h"
#include "sd_lvgl_fs.h"

/**
 * @brief Chat GPT system prompt. This preambles instructs Chat GPT
 * to return a specific response based on intent.
 */
const char ChatSystemPrompt[] = "You are Watt-IZ. Always return exactly one JSON object in your reply. "
"Rules of response: "
"1. If the user is chatting normally, respond with: "
"{\\\"mode\\\": \\\"chat\\\", \\\"reply\\\": \\\"<your short conversational reply>\\\" } "
"2. If the user is asking the device to do something, respond with: "
"{\\\"mode\\\": \\\"command\\\", \\\"intent\\\": \\\"<intent>\\\", \\\"device\\\": \\\"<device>\\\", \\\"action\\\": \\\"<action>\\\", \\\"reply\\\": \\\"<short acknowledgement>\\\" } "
"3. If the system cannot perform the request, respond with: "
"{\\\"mode\\\": \\\"unknown\\\", \\\"reply\\\": \\\"<reason for denial>\\\" } "
"No text outside the JSON object. Replies must be concise for text-to-speech.";

// const char PromptChatControl[] = "You are Watt-IZ, an embedded voice assistant in a small hardware device. "
// "Read the user's text and determine whether they are having a normal chat "
// "or asking you to do something. Always answer with a single JSON object."
// "For chat, respond with:"
// "{"
// "\"mode\": \"chat\","
// "\"reply\": \"<your conversational reply>\" "
// "}"
// "For commands, respond with:"
// "{"
// "\"mode\": \"command\", "
// "\"intent\": \"<type of command>\", "
// "\"device\": \"<device name if relevant>\", "
// "\"action\": \"<action>\", "
// "\"reply\": \"<short confirmation>\" "
// "}"
// "Keep replies short and easy to read aloud. No markdown. No extra text. ";

// // This prompt is used when the chat message is strictly CHAT
// const char PromptChatOnly[] = "You are Watt-IZ, a concise voice assistant. "
// "Respond naturally and conversationally. "
// "Do not output JSON. Keep replies short for text-to-speech.";

/**
 * @brief Language definitions for speech services.
 * See link for language codes & voice name options:
 * https://www.science.co.il/language/Locale-codes.php
 */
#define GOOGLE_LANGUAGE                   "en-US"  
#define LANG_VOICE_FEMALE                 "en-US-Wavenet-G" 
#define LANG_VOICE_MALE                   "en-GB-Wavenet-B"
#define AUDIO_ENCODING_TYPE               "LINEAR16"

#define STT_CHUNK_SIZE                    1536     // chunk size before base64 encoding
#define STT_ENC_CHUNK_SIZE                2048     // chunk size after base64 encoding (4/3)
#define SOURCE_FROM_FILE                  0
#define SOURCE_FROM_STREAM                1

#define MAX_TTS_STRLEN                    1024
#define TTS_CHUNK_SIZE                    4096     // this should be powers of 2
#define TTS_DEC_FILE_SIZE                 3072     // TTS_CHUNK_SIZE (decoded) * 0.75
#define TTS_PREAMBLE_LEN                  21
#define WAV_HDR_SIZE                      44

#define SYS_CRED_FILENAME                 "/wattiz_config.json"

#define TRANSLATE_TEXT_BUFR_SIZE          2048  // make big enough for target text 



//###################################################################
// ENABLE SPEECH SERVICES - Disable services not used to save a small
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
      bool translate(char *lang_from, char *lang_to);
      // PSRAM memory for text input & output
      char *translateSourceText;          // source text of a translate oeration
      char *translateResultsText;         // results (output) of translate operation        
      
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
      bool convert(char *text, bool play_audio, const char *filepath, 
            int16_t volume, const char *lang_code, const char *voice_name, float speak_rate=1.0);     

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
      bool convert(uint16_t mode=SOURCE_FROM_FILE, const char *speech_file=nullptr, 
            const char *stream_out_file=nullptr, const char *lang_code=GOOGLE_LANGUAGE);  // convert audio to text
      void setClientTimeout(uint32_t timeout_ms);  // set I/O timeout in milliseconds
      uint32_t getClientTimeout(void);    // returns the I/O timeout in milliseconds
      // bool clientKeepAlive(void);      
      bool clientReconnect(void);
      bool isClientConnected(void);       // try to reconnect sttClient after disconnect

      bool debugClient(uint8_t inum, uint16_t data_sent, uint16_t enc_len);     // check connect and print any response from host

      // WiFiClientSecure sttClient;         
      String sttResponse;                 // text response from transcribeSpeechToText
      String sttConfidence;               // confidence of transcribe

   private:
      bool sendChunk(WiFiClientSecure &client, const uint8_t *data, size_t len);
      bool endChunks(WiFiClientSecure &client);
      String sttApiKey;
      String sttApiServer;
      String sttApiEndPoint;
};


/********************************************************************
 * @brief Constant data for the Base64 encoder
 */
static const char B64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


/********************************************************************
* @brief Create a public class for the Base64 encoder
*/
struct B64Stream 
{
  uint8_t carry[2];
  uint8_t carry_len = 0;

  // Encode input into out (ASCII), return produced length
  size_t encode(const uint8_t *in, size_t inLen, char *out, size_t outCap) {
    size_t o = 0;
    size_t i = 0;

    // If we have carry, try to complete a 3-byte group
    if (carry_len > 0) {
      uint8_t tmp[3];
      tmp[0] = carry[0];
      if (carry_len == 2) tmp[1] = carry[1];

      if (carry_len == 1 && inLen >= 2) {
        tmp[1] = in[0];
        tmp[2] = in[1];
        i = 2;
        carry_len = 0;

        if (o + 4 <= outCap) {
          out[o++] = B64[(tmp[0] >> 2) & 0x3F];
          out[o++] = B64[((tmp[0] & 0x03) << 4) | ((tmp[1] >> 4) & 0x0F)];
          out[o++] = B64[((tmp[1] & 0x0F) << 2) | ((tmp[2] >> 6) & 0x03)];
          out[o++] = B64[tmp[2] & 0x3F];
        }
      } else if (carry_len == 2 && inLen >= 1) {
        tmp[2] = in[0];
        i = 1;
        carry_len = 0;

        if (o + 4 <= outCap) {
          out[o++] = B64[(tmp[0] >> 2) & 0x3F];
          out[o++] = B64[((tmp[0] & 0x03) << 4) | ((tmp[1] >> 4) & 0x0F)];
          out[o++] = B64[((tmp[1] & 0x0F) << 2) | ((tmp[2] >> 6) & 0x03)];
          out[o++] = B64[tmp[2] & 0x3F];
        }
      } else {
        // Not enough input to complete; fall through to carry accumulation below
        i = 0;
      }
    }

    // Encode full 3-byte groups
    for (; i + 3 <= inLen; i += 3) {
      if (o + 4 > outCap) break;
      uint8_t b0 = in[i], b1 = in[i+1], b2 = in[i+2];
      out[o++] = B64[(b0 >> 2) & 0x3F];
      out[o++] = B64[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
      out[o++] = B64[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)];
      out[o++] = B64[b2 & 0x3F];
    }

    // Store leftover 1–2 bytes as carry
    size_t rem = inLen - i;
    if (rem > 0) {
      carry[0] = in[i];
      carry_len = 1;
      if (rem == 2) {
        carry[1] = in[i+1];
        carry_len = 2;
      }
    }
    return o;
  }

  // Flush carry with '=' padding
  size_t flush(char *out, size_t outCap) {
    if (carry_len == 0) return 0;
    if (outCap < 4) return 0;

    uint8_t b0 = carry[0];
    uint8_t b1 = (carry_len == 2) ? carry[1] : 0;

    out[0] = B64[(b0 >> 2) & 0x3F];
    out[1] = B64[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];

    if (carry_len == 2) {
      out[2] = B64[((b1 & 0x0F) << 2) & 0x3F];
      out[3] = '=';
    } else { // carry_len == 1
      out[2] = '=';
      out[3] = '=';
    }

    carry_len = 0;
    return 4;
  }
};

#endif         // end of USE_SPEECH_TO_TEXT_SERVICE directive


#if defined(USE_CHAT_GPT_SERVICE)
//###################################################################
// CHAT GPT CLASS - Make a text request of openAI Chat GPT service.
// Note: Call initChatGPT() prior to calls to request(). 
//###################################################################
class CHAT_GPT {
   public:
      CHAT_GPT();                         // constructor
      ~CHAT_GPT();                        // destructor
      bool init(void);                    // load chat GPT credentials from SD card
      bool request(const char *req_text);  // call chatbot with text
      // response strings
      String gpt_model;
      String chat_response;    
      String response_mode;  
      String intent;
      String device;
      String action;      

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

