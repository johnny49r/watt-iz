#include "speechServices.h"
#include "base64.hpp"      // included here because code is wrapped in an .hpp file


// Enable speech class objects
#if defined (USE_CHAT_GPT_SERVICE)
   CHAT_GPT chatGPT;
#endif

#if defined (USE_SPEECH_TO_TEXT_SERVICE)
   SPEECH_TO_TEXT speechToText;
#endif

#if defined (USE_TEXT_TO_SPEECH_SERVICE)
   TEXT_TO_SPEECH textToSpeech;
#endif

#if defined (USE_LANGUAGE_TRANSLATE_SERVICE)
   LANGUAGE_TRANSLATE language_xlate;
#endif


/*###################################################################
 *                      Language Translate class 
 ##################################################################*/

 /**
  * @brief Language Translate contructor
  */
LANGUAGE_TRANSLATE::LANGUAGE_TRANSLATE()
{
   // empty constructor
}

/********************************************************************
*  @brief Language Translate  destructor
*/
LANGUAGE_TRANSLATE::~LANGUAGE_TRANSLATE() 
{
   // empty destructor
}


/********************************************************************
 * @brief Intialize translate credentials.
 */
bool LANGUAGE_TRANSLATE::init(void)
{
   langApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   langApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   langApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "translate_endpoint");
   if(langApiKey.length() == 0 || langApiServer.length() == 0 || langApiEndPoint.length() == 0)
      return false;
   return true;
}


/********************************************************************
 * @brief Run translation.
 * @param: translateText - transcribe from language
 * @param: lang_from - Google language code <from>. Example: "en-US" (english US)
 * @param: lang_to - Google language code <to>. Example: "th-TH" (Thai)
 */
String LANGUAGE_TRANSLATE::translateLanguage(const char *input_text, char *lang_from, char *lang_to)
{
   HTTPClient http;
   String Translated;
   Translated.clear();

   if(langApiEndPoint.isEmpty() || langApiKey.isEmpty())
      return "";

   String url = langApiEndPoint + langApiKey;

   http.begin(url);
   http.addHeader("Content-Type", "application/json");

   JsonDocument doc;
   doc["q"] = input_text;                 // text to translate
   doc["target"] = lang_to;               // target language code
   doc["source"] = lang_from;             // source language code (optional; auto-detect if omitted)
   doc["format"] = "text";                // <<— IMPORTANT: avoid HTML entities in output

   String body;
   serializeJson(doc, body);

   int httpCode = http.POST(body);
   if (httpCode <= 0) {
      Serial.printf("HTTP error: %s\n", http.errorToString(httpCode).c_str());
      http.end();
      return "";
   }

   String payload = http.getString();
   http.end();
         Serial.printf("payload=%s\n", payload.c_str());

   JsonDocument resp;
   if (deserializeJson(resp, payload)) {
      Serial.println("JSON parse failed");
      return "";
   }

   Translated = resp["data"]["translations"][0]["translatedText"].as<String>();
   return Translated;
}



/*###################################################################
 *                      Speech To Text class 
 ##################################################################*/

#if defined (USE_SPEECH_TO_TEXT_SERVICE)
/********************************************************************
*  @brief CSpeech To Text class constructor
*/
SPEECH_TO_TEXT::SPEECH_TO_TEXT() 
{
   // empty constructor
}


/********************************************************************
*  @brief CSpeech To Text class destructor
*/
SPEECH_TO_TEXT::~SPEECH_TO_TEXT() 
{
   sttClient.stop();
}


/********************************************************************
 * @brief Load Speech To Text credentials from config file on SD Card
 * @return true if all creds are loaded successfully.
 */
bool SPEECH_TO_TEXT::init(void)
{
   sttApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   sttApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   sttApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_endpoint");
   if(sttApiKey.length() == 0 || sttApiServer.length() == 0 || sttApiEndPoint.length() == 0)
      return false;
       
   return true;
}


/********************************************************************
*  @fn Reconnect stt client after timeout or random disconnect
*/
bool SPEECH_TO_TEXT::clientReconnect(void)
{
   uint32_t tmo = millis();
   int16_t ret = 0;

   sttClient.stop();                      // stop the client
   vTaskDelay(250);

   while((millis() - tmo) < 2000) {       // wait a while for connect
      ret = sttClient.connect(sttApiServer.c_str(), 443, 200); 
         Serial.printf("api server=%s\n", sttApiServer.c_str());
      if(ret > 0) break;
      vTaskDelay(10);
   }
   return (ret > 0);
}


/********************************************************************
*  @fn Return stt client timeout in seconds
*/
uint32_t SPEECH_TO_TEXT::getClientTimeout(void)
{
   return sttClient.getTimeout();
}


/********************************************************************
*  @fn Set sttClient connection timeout in milliseconds
*/
void SPEECH_TO_TEXT::setClientTimeout(uint32_t timeout_ms)
{
   sttClient.setTimeout(timeout_ms);
}


/********************************************************************
*  @fn Return client connection status 
*  @return true if client is connected to server, false otherwise
*/
bool SPEECH_TO_TEXT::isClientConnected(void)
{
   return sttClient.connected();
}


/********************************************************************
 * @brief Ping the sttClient to keep it connected.
 * @note: This function should be called periodically (maybe every minute)
 * to prevent sttClient from timing out and disconnecting. This can 
 * prevent long delays if the client has to reconnect.
 */
bool SPEECH_TO_TEXT::clientKeepAlive(void)
{
   if (sttClient.connected()) {
      Serial.println("→ sending keep-alive ping");
      // A harmless newline or comment works for many text protocols
      sttClient.write((const uint8_t *)"\r\n", 2);
      sttClient.flush();
   } else {
      Serial.println("⚠ disconnected, reconnecting...");
      return clientReconnect();
   }

   // Read or stream as needed
   while (sttClient.available()) {
      Serial.write(sttClient.read());
   }

   return true;
}


/********************************************************************
*  @brief Connect to Google Speech to Text 'Transcribe' service and send
*  encoded WAV file. Text will be returned.
*  NOTE: System must be connected to the internet.
*  NOTE: Caller must have a valid google API key.
*/
bool SPEECH_TO_TEXT::transcribeSpeechToText(const char *filepath, char *lang_code)
{
   uint32_t timeout;
   uint32_t file_chunk_size = 24576;      // size of data read from file
   uint32_t encoded_chunk_size = 32768;   // size of data after encoding (4/3 ratio)
   int32_t voice_data_size;
   uint32_t encode_len; 
   bool ret = false;
   char temp;
   uint32_t data_sent, audio_file_index, i;
   int32_t bytes_rcvd;
   File _file;

   // Can't do this without wifi connected
   if(WiFi.status() != WL_CONNECTED) {
      return false;
   }

   WiFi.setSleep(WIFI_PS_NONE);              // keep connection open

   /**
    * @brief Connect to transcribe server
    */
   if(!sttClient.connected()) {               // still connected to server?
#if defined (USE_CA_CERTIFICATE)
      const char *ca_cert = {CERT_CA_STRING};
      sttClient.setCACert(ca_cert);                // more secure connection 
#else  
      sttClient.setInsecure();               // connect without ca cert
#endif       
      if(sttClient.connect(sttApiServer.c_str(), 443, 500) <= 0) {
         Serial.println("sttClient Connection failed!");
         return ret;
      }
   }

   // Get WAV file data size. File is assumed to have a WAV header
   voice_data_size = sd.fsize(filepath) - (WAV_HEADER_SIZE + 4);
   if(voice_data_size < 1)
      return ret;

   /**
    * @brief Create buffers for reading and encoding voice data
    */
   uint8_t *chunkBufr = (uint8_t *)heap_caps_malloc(file_chunk_size + 64, MALLOC_CAP_SPIRAM);
   if(!chunkBufr)
      return ret;

   uint8_t *encodeBufr = (uint8_t *)heap_caps_malloc(encoded_chunk_size + 64, MALLOC_CAP_SPIRAM);
   if(!encodeBufr) {
      free(chunkBufr);
      return ret;      
   }      

   /**
    * @brief Build the Http body and send to transcribe server 
    * Language code for english: en-US
    */
   String HttpBody1 = 
         "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":16000,"
         "\"languageCode\":\"" + String(lang_code) + 
         "\"},\"audio\":{\"content\":\"";
   String HttpBody3 = "\"}}\r\n\r\n";
   int32_t httpBody2Length = (voice_data_size + WAV_HEADER_SIZE + 4) * 4/3;  // 4/3 is from base64 encoding ratio
   String ContentLength = String(HttpBody1.length() + httpBody2Length + HttpBody3.length()); 
   String HttpHeader = String("POST /v1/speech:recognize?key=") + sttApiKey +
         String(" HTTP/1.1\r\nHost: ") + sttApiServer + 
         String("\r\nContent-Type: application/json\r\nContent-Length: ") + 
         ContentLength + String("\r\n\r\n");

   /**
    * @brief Open voice file for reading, encode the data, and send to transcribe server
    */
   _file = sd.fopen(filepath, FILE_READ, false);
   if(!_file) {
      free(chunkBufr);                    // free local chunk memory
      free(encodeBufr);
      Serial.printf("Error: Can't open file: %s\n", filepath);
      return ret;
   }

   /**
    * @brief Print header and body prefix strings
    */
   HttpHeader.toCharArray((char *)chunkBufr, file_chunk_size);
   sttClient.write(chunkBufr, HttpHeader.length());

   HttpBody1.toCharArray((char *)chunkBufr, file_chunk_size);
   sttClient.write(chunkBufr, HttpBody1.length());
   
   /**
    * @brief Encode the WAV header and send to transcribe server
    */
   bytes_rcvd = sd.fread(_file, chunkBufr, WAV_HEADER_SIZE + 4, 0); 
   encode_len = encode_base64((uint8_t *)chunkBufr, WAV_HEADER_SIZE + 4, (uint8_t *)encodeBufr);
   data_sent = sttClient.write(encodeBufr, encode_len);

   /**
    * @brief Loop to encode and send voice data to the transcribe service
    */
   audio_file_index = WAV_HEADER_SIZE + 4;   // start of data in file
   while(true) {
      // Encode the mic data with base64 and send to server
      if(sttClient.connected()) {
         bytes_rcvd = sd.fread(_file, chunkBufr, file_chunk_size, audio_file_index);      
         if(bytes_rcvd <= 0)              // exit if no more data to read
            break;    

         audio_file_index += bytes_rcvd;           // seek pointer to next chunk in file

         // Add base64 encoding to audio chunk 
         encode_len = encode_base64((uint8_t *)chunkBufr, bytes_rcvd, (uint8_t *)encodeBufr);
         // send to google stt service. Make sure all data is sent.
         data_sent = 0;
         timeout = millis();
         // handle multiple partial writes
         while((millis() - timeout) < 1000) {   // failsafe timeout
            data_sent += sttClient.write(encodeBufr + data_sent, encode_len - data_sent);
            if(data_sent >= encode_len || data_sent == 0) 
               break;
         } 
         if(data_sent == 0) {             // exit if client has closed
            break;
         }  
      } else 
         break; 
   } 
   sd.fclose(_file);                       // close the file opened above

   // Voice data sent, send the remainder of the http body
   sttClient.print(HttpBody3);     
   
   /**
    * @brief Wait for response from STT service
    */
   String STT_Response = "";     
   timeout = millis();
   while (!sttClient.available() && (millis() - timeout) < 10000) { 
      vTaskDelay(20); 
   }

   if((millis() - timeout) >= 10000 ) {
      free(chunkBufr);                       // free local chunk memory
      free(encodeBufr);
      sttResponse = "Client response timeout";
      return ret;
   }

   // Read JSON text response data into String object
   while (sttClient.available()) {
      char temp = sttClient.read();
      STT_Response = STT_Response + temp;
   }

   /**
    * @brief Extract text response from JSON document
    */
   JsonDocument doc;
  
   // Ignore text up to the start of the json document
   int16_t position = STT_Response.indexOf('{');       // json doc starts with first '{'
   sttResponse = STT_Response.substring(position);

   // deserialize json format so we can search for text segments
   DeserializationError error = deserializeJson(doc, sttResponse);
   if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      sttResponse = "transcript unknown";
   } else {
      JsonArray results = doc["results"].as<JsonArray>();
      String fullTranscript;

      // Iterate through all possible "alternatives" and concatenate the results.
      for (JsonObject result : results) {
         JsonArray alternatives = result["alternatives"].as<JsonArray>();

         // Pick the best (first) alternative 
         if (!alternatives.isNull() && alternatives.size() > 0) {
            String transcript = alternatives[0]["transcript"].as<String>(); // only use the first alternative
            
            // Insert a space between two strings that have no spaces at end or beginning
            if (fullTranscript.length() > 0 && !fullTranscript.endsWith(" ") && !transcript.startsWith(" ")) 
               fullTranscript += " "; // space separator
            fullTranscript += transcript;
         }
      }
      sttResponse = fullTranscript;
   }

   // determine pass/fail response
   if(sttResponse.equals("null") || sttResponse.isEmpty())
      ret = false;
   else 
      ret = true;
   free(chunkBufr);                       // free local chunk memory in PSRAM
   free(encodeBufr);
         // Serial.printf("STT Response: %s\n", sttResponse.c_str());
   return ret;
}
#endif


/*###################################################################
 *                      Text To Speech class 
 ##################################################################*/

#if defined(USE_TEXT_TO_SPEECH_SERVICE)
 /********************************************************************
*  @brief Text To Speech class constructor
*/
TEXT_TO_SPEECH::TEXT_TO_SPEECH() {
   // empty constructor
}


/********************************************************************
*  @brief Text To Speech class destructor
*/
TEXT_TO_SPEECH::~TEXT_TO_SPEECH() {
   // empty destructor
}


/********************************************************************
 * @brief Load credentials for text to speech service.
 */
bool TEXT_TO_SPEECH::init(void)
{
   ttsApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   ttsApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   ttsApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_endpoint");
   if(ttsApiKey.length() == 0 || ttsApiServer.length() == 0 || ttsApiEndPoint.length() == 0)
      return false;
   return true;   
}


/********************************************************************
* @brief Google text to speech service. Caller must have an API key (same 
* as the speech to text service).
* @param text - the text to convert to speech.
* @param play_audio - just what it says!
* @param filepath - path/filename of a file to save audio data.
* @param volume - volume (if play_audio), 0 - 100%
* @param lang_code - what language is to be spoken.
* @return true if successful.
* NOTE: System must be connected to the internet.
*/
bool TEXT_TO_SPEECH::transcribeTextToSpeech(String text, bool play_audio, 
      const char *filepath, int16_t volume, char *lang_code, char *voice_name, float speak_rate) 
{   
   size_t bytesReceived;
   uint16_t chunkCntr = 0;
   char *acptr;
   uint32_t dec_length;
   uint32_t i, offset, mod_len;
   uint32_t idx = 0;
   uint32_t tmo;
   File _file;
   bool file_is_open = false;
   #define WAV_HDR_SIZE          44
   #define MAX_SLABS             1024

   // ---- Slab entry (lives in PSRAM) ----
   struct SlabEntry {
      uint8_t     *ptr;     // pointer to buffer in PSRAM
      uint32_t    used;    // actual used size
   };
   // ---- Slab table (lives in PSRAM) ----
   SlabEntry* slabTable = nullptr;
   uint32_t slabCount = 0;
   char *p;

   // delete old duplicate file if it exists
   if(filepath) 
      sd.fremove(filepath);

   /**
   *  @brief Remove unsupported escape sequences from text 
   */
   for(i=0; i<text.length(); i++) {
      if(text.charAt(i) == 92 || text.charAt(i) == 34)   // remove '\' or '"' chars
         text.setCharAt(i, ' ');
   }

   /**
    * @brief Create an array of pointers to 'slab buffers'. 1 second of audio will
    * use approx 16 slab buffers, therefore 1 minute of audio will be approx 960 slab buffers
    */
   slabTable = (SlabEntry*) heap_caps_malloc(MAX_SLABS * sizeof(SlabEntry),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
   if (!slabTable) {
      Serial.println(F("Error: Failed to allocate slab table in PSRAM!"));
      return false;
   }

   /**
    *  @brief Create response buffer
    */
   char *response_bufr = (char *)heap_caps_malloc(TTS_CHUNK_SIZE + 64, MALLOC_CAP_SPIRAM); 
   if(!response_bufr) {
      Serial.println(F("Error: No memory for response_bufr!"));
      return false;    
   }

   /**
    *  @brief initialize wifi & http clients
    */
   static WiFiClientSecure ttsClient;
   ttsClient.setInsecure();               // set secure client insecure!
   static HTTPClient httpClient;
   httpClient.useHTTP10(true);            // IMPORTANT: Use older HTTP 1.0 to get continuous response data
   // concatenate google endpoint and api key into one string
   String GoogleApiKey = ttsApiEndPoint + ttsApiKey;            
   httpClient.begin(ttsClient, GoogleApiKey);   

      // Serial.printf("TTS text=%s, lang_code=%s, voice_name=%s\n", text.c_str(), lang_code, voice_name);

   /**
   *  @brief Send JSON request body to google TTS service
   */
   JsonDocument doc;
   doc["input"]["text"] = text;
   doc["voice"]["languageCode"] = lang_code; //TTS_LANGUAGE;
   if(!voice_name)
      doc["voice"]["name"] = TTS_VOICE;   // default english voice "en-US-Wavenet-G" female voice
   else 
      doc["voice"]["name"] = voice_name;   // Example: "en-US-Wavenet-G" female voice
   doc["audioConfig"]["audioEncoding"] = "LINEAR16";
   doc["audioConfig"]["sampleRateHertz"] = 16000;
   doc["audioConfig"]["speakingRate"] = speak_rate;   // 0.0-1.0. If talking too fast - slow it down!
   String requestBody;
   serializeJson(doc, requestBody);       // convert json to a String object
   httpClient.addHeader("Content-Type", "application/json");
   int httpCode = httpClient.POST(requestBody);

   /**
   *  @brief Get audio response data from the google TTS service
   */
   while (httpClient.connected()) {
      /**
      *  @brief First chunk in json format contains the marker "audioContent"
      */
      if(chunkCntr == 0) {
         bytesReceived = httpClient.getStream().readBytes((char *)response_bufr, TTS_CHUNK_SIZE);
         if (bytesReceived == 0) {        // exit if all done
            break;
         }
         acptr = strstr((char *)response_bufr, "audioContent");   // skip past start of encoded data
         if(!acptr) {                     // exit if keyword not found
            Serial.println(F("Keyword <audioContent> not found!"));
            break;        
         }
         acptr = strstr((char *)acptr, ":");    // search for the colon after the audioContent identifier
         acptr += 3;                      // advance index to start of encoded data
         offset = (uint32_t)acptr - (uint32_t)response_bufr;   // calc abs memory pointer

         /**
         *  @brief Add more data to align incomming data to the base64 decode boundary size (8 bytes)
         */
         mod_len = 8 - ((bytesReceived-offset) % 8);     // align to 8 bytes (mod 0 to both 4 and 3 byte lengths)
         idx = (bytesReceived-offset) + mod_len;         // total bytes in first chunk read
         bytesReceived = httpClient.getStream().readBytes((char *)response_bufr+bytesReceived, mod_len);   // add odd data to end of the buffer
         if (mod_len > 0 && bytesReceived == 0) {        // all done?
            break;
         }           
         // save decoded audio data in PSRAM buffer array.
         slabTable[slabCount].ptr = (uint8_t *)heap_caps_malloc(TTS_DEC_FILE_SIZE, MALLOC_CAP_SPIRAM); 
         slabTable[slabCount].used = decode_base64((unsigned char *)acptr, idx, 
                  (unsigned char *)slabTable[slabCount].ptr);

         /**
          * If play audio, play one chunk from the slab buffer
          */
         if(play_audio) {
            audio.playAudioMem((uint8_t *)slabTable[slabCount].ptr+WAV_HDR_SIZE, 
                     slabTable[slabCount].used-WAV_HDR_SIZE, volume);    
         }
         // increment to next buffer
         slabCount++;
         
      } else {                            // Additional chunks are data only       
         bytesReceived = httpClient.getStream().readBytes(response_bufr, TTS_CHUNK_SIZE);     

         // Add new PSRAM buffer to the slab array
         slabTable[slabCount].ptr = (uint8_t *)heap_caps_malloc(TTS_DEC_FILE_SIZE, MALLOC_CAP_SPIRAM); 
         slabTable[slabCount].used = decode_base64((unsigned char *)response_bufr, bytesReceived, 
                  (unsigned char *)slabTable[slabCount].ptr);
          
         if(play_audio)
            audio.playAudioMem((uint8_t *)slabTable[slabCount].ptr, slabTable[slabCount].used, volume);     
         // increment to next buffer
         slabCount++;                   
      }      
      chunkCntr++;      
   }
         // Serial.printf("chunks=%d, total resp size (idx)=%d\n", chunkCntr, idx);
         // sys_utils.hexDump((uint8_t *)json, TTS_CHUNK_SIZE);   //*****************************
   httpClient.end();
      // Serial.printf("Status Code: %d\n", httpCode);
   if(httpCode != 200)              // code 200 = success, all others bad!
      Serial.printf("Error: TTS http client, code=%d\n", httpCode);

   /**
    * Write the collective buffers to a file on the SD Card if 'filepath'. 
    * PSRAM slab memory is also free'd here.
    */
   if(filepath) {
      _file = sd.fopen(filepath, FILE_APPEND, true);
      file_is_open = (_file);
   }
   for (i = 0; i < slabCount; i++) {
      if (slabTable[i].ptr) {
         if(file_is_open) {
            sd.fwrite(_file, slabTable[i].ptr, slabTable[i].used);
         }
         heap_caps_free(slabTable[i].ptr);   // free the memory buffer
         slabTable[i].ptr = nullptr;       
      }
   }
   if(file_is_open) 
      sd.fclose(_file);        // close the file if previously opened

   // free the response memory used in PSRAM
   heap_caps_free(response_bufr);   

   // finally free the slab table in PSRAM
   heap_caps_free(slabTable);   
 
   return true;
}
#endif


/*###################################################################
 *                         Chat GPT class 
 ##################################################################*/
#if defined (USE_CHAT_GPT_SERVICE)

/********************************************************************
*  @brief CHAT_GPT constructor
*/
CHAT_GPT::CHAT_GPT() {
   // empty
}


/********************************************************************
*  @brief CHAT_GPT destructor
*/
CHAT_GPT::~CHAT_GPT() {
   // empty
}

/********************************************************************
*  @fn Initialize ChatGPT credentials (API key, endpoint, chat version)
*/
bool CHAT_GPT::initChatGPT(void)
{
   ChatGPTApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "api_key");
   ChatGPTEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "api_endpoint");
   ChatGPTVersion = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "chat_version");

   if(ChatGPTApiKey.length() == 0 || ChatGPTEndPoint.length() == 0 || ChatGPTVersion.length())
      return false;
   else
      return true;
}


/********************************************************************
*  @brief Send text request to chat GPT 
*/
String CHAT_GPT::chatRequest(const char *req_text, bool enab_tts, uint16_t volume)
{
   uint16_t i;
   static String content;
   String gpt_model;

   if(ChatGPTApiKey.length() == 0) {
      return "Error: Can't access OpenAI API Key!";
   }

   String Request = req_text;
   String Response = sendToChatGPT(Request);

   JsonDocument chat_doc;
   DeserializationError error = deserializeJson(chat_doc, Response);
   if (error) {
      return "DeserializeJson() failed!";
   } else {
      content = chat_doc["choices"][0]["message"]["content"].as<String>();
      gpt_model = chat_doc["model"].as<String>();
            Serial.print("GPT model: "); Serial.println(gpt_model);
      if(content.length() < 1) {
         content = "No response from Chat-GPT!";
      } else {
         for(i=0; i<content.length(); i++) {
            if(content.charAt(i) == 92 || content.charAt(i) == 34)   // remove '\' or '"' chars
               content.setCharAt(i, ' ');
         }       
      }                                         
   }
   return content;
}


/********************************************************************
 * @brief Helper function sends 'message' to Chat GPT server.
 */
String CHAT_GPT::sendToChatGPT(const String& message) 
{
   if (ChatGPTApiKey.length() == 0 || ChatGPTEndPoint.length() == 0) {
      return "ChatGPT Credentials Not Set!";
   }

   String sanitizedMessage = sanitizeString(message);
   String payload = "{\"model\": \"" + ChatGPTVersion + "\", \"messages\": [{\"role\": \"user\", \"content\": \"" + sanitizedMessage + "\"}]}";

   return makeHttpRequest(payload);
}


/********************************************************************
 * @brief Make an HTTP request to the Chat GPT server (helper function).
 */
String CHAT_GPT::makeHttpRequest(const String& payload) 
{
   HTTPClient http;
   WiFiClientSecure client;

   // Demo uses insecure connection. Google CA Cert if you need more security.
   client.setInsecure();

   if (!http.begin(client, ChatGPTEndPoint)) {    // initialize & check endpoint
      Serial.println("Error: HTTP begin failed.");
      return "HTTP begin failed.";
   }
   
   http.setTimeout(10000);  // Timeout = 10 seconds if not probably null response
   http.addHeader("Content-Type", "application/json");
   http.addHeader("Accept", "application/json");
   
   String authHeader = "Bearer " + ChatGPTApiKey;
   Serial.print("Using Authorization header: ");
   Serial.println(authHeader);
   http.addHeader("Authorization", authHeader);

   int retryCount = 0;
   int maxRetries = 2;
   int httpResponseCode = -1;
   String response;
   
   while (retryCount <= maxRetries) {
      httpResponseCode = http.POST(payload);
      if (httpResponseCode > 0) {
         break;
      } else {
         Serial.print("HTTP POST failed on try ");
         Serial.print(retryCount + 1);
         Serial.print(". Error: ");
         Serial.println(http.errorToString(httpResponseCode));
         retryCount++;
         delay(1000);
      }
   }
   
   if (httpResponseCode > 0) {
      if (httpResponseCode >= 200 && httpResponseCode < 300) {
         response = http.getString();
         Serial.printf("HTTP Response code: %d\nResponse body: %s\n", httpResponseCode, response.c_str());
      } else {
         response = "HTTP POST returned code: " + String(httpResponseCode) + ". Response: " + http.getString();
         Serial.println(response);
      }
   } else {
      response = "HTTP POST failed after retries. Error: " + String(http.errorToString(httpResponseCode).c_str());
      Serial.println(response);
   }

   http.end();
   return response;
}


/********************************************************************
 * @brief Helper function to clean bogus characters from message string
 */
String CHAT_GPT::sanitizeString(const String& input) 
{
   String sanitized = input;
   sanitized.replace("\\", "\\\\");
   sanitized.replace("\"", "\\\"");
   sanitized.replace("\n", "\\n");
   sanitized.replace("\r", "\\r");
   sanitized.replace("\t", "\\t");

   for (int i = 0; i < sanitized.length(); i++) {
      if (sanitized[i] < 32 && sanitized[i] != '\n' && sanitized[i] != '\r' && sanitized[i] != '\t') {
         sanitized.remove(i, 1);
         i--;
      }
   }

   return sanitized;
}
#endif
