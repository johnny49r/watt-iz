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

/*###################################################################
 *                      Speech To Text class 
 ##################################################################*/

#if defined (USE_SPEECH_TO_TEXT_SERVICE)
/********************************************************************
*  @brief CSpeech To Text class constructor
*/
SPEECH_TO_TEXT::SPEECH_TO_TEXT() {
   // empty
}


/********************************************************************
*  @brief CSpeech To Text class destructor
*/
SPEECH_TO_TEXT::~SPEECH_TO_TEXT() {
   // empty
}


/********************************************************************
 * @brief Load Speech To Text credentials from config file on SD Card
 * @return true if all creds are loaded successfully.
 */
bool SPEECH_TO_TEXT::init(void)
{
   sttApiKey = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_key");
   sttApiServer = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_server");
   sttApiEndPoint = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_endpoint");
   if(sttApiKey.length() == 0 || sttApiServer.length() == 0 || sttApiEndPoint.length() == 0)
      return false;
   return true;
}


/********************************************************************
*  @fn Reconnect stt client after timeout or random disconnect
*/
bool SPEECH_TO_TEXT::clientReconnect(void)
{
   sttClient.stop();
   vTaskDelay(150);
   return sttClient.connect(sttApiServer.c_str(), 443, 200);
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
   if (!sttClient.connected()) return false;

   // Minimal HTTP/1.1 ping
   sttClient.print(
      String("HEAD / HTTP/1.1\r\n") +
      "Host: " + sttApiServer + "\r\n" +
      "Connection: keep-alive\r\n" +
      "\r\n"
   );

   // Quickly read response headers to avoid unread data piling up
   uint32_t deadline = millis() + 1500;
   while (millis() < deadline) {
      while (sttClient.available()) {
         char c = sttClient.read();
         // End of headers?
         static uint32_t match = 0;
         if ((match == 0 && c == '\r') ||
            (match == 1 && c == '\n') ||
            (match == 2 && c == '\r') ||
            (match == 3 && c == '\n')) {
            match++;
            if (match == 4) return true;  // got "\r\n\r\n"
         } else {
            match = (c == '\r') ? 1 : 0;
         }
      }
      delay(5);
      if (!sttClient.connected()) return false;
  }
  // Timed out reading headers—treat as failure
  return false;
}


/********************************************************************
*  @brief Connect to Google Speech to Text 'Transcribe' service and send
*  encoded WAV file. Text will be returned.
*  NOTE: System must be connected to the internet.
*  NOTE: Caller must have a valid google API key.
*/
bool SPEECH_TO_TEXT::transcribeSpeechToText(const char *filepath) 
{
   uint32_t timeout;
   int32_t voice_data_size;
   bool ret = false;
   uint32_t data_sent, abs_idx, i;
   int32_t bytes_rcvd;

   #if defined (USE_CA_CERTIFICATE)
      const char *ca_cert = {CERT_CA_STRING};
      sttClient.setCACert(ca_cert);                // more secure connection 
   #else  
      sttClient.setInsecure();               // connect without ca cert
   #endif   

   /**
    * @brief Connect to transcribe server
    */
   if(!sttClient.connected()) {               // still connected to server?
      Serial.println("sttClient not connected - trying to reconnect!");
      if(!clientReconnect()) {
         Serial.println(F("Server Connection failed!"));
         return ret;
      } 
   }

   // Get WAV file data size. File is assumed to have a WAV header
   voice_data_size = sdcard.getFileSize(filepath) - (WAV_HEADER_SIZE + 4);
   if(voice_data_size < 1)
      return ret;

   /**
    * @brief Create buffers for reading and encoding voice data
    */
   uint8_t *chunkBufr = (uint8_t *)heap_caps_malloc(STT_CHUNK_SIZE + 16, MALLOC_CAP_SPIRAM);
   if(!chunkBufr)
      return ret;

   uint8_t *encodeBufr = (uint8_t *)heap_caps_malloc(STT_ENC_CHUNK_SIZE + 16, MALLOC_CAP_SPIRAM);
   if(!encodeBufr) {
      free(chunkBufr);
      return ret;      
   }      

   /**
    * @brief Build the Http body and send to transcribe server
    */
   String HttpBody1 = "{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":16000,\"languageCode\":\"en-US\"},\"audio\":{\"content\":\"";
   String HttpBody3 = "\"}}\r\n\r\n";
   int32_t httpBody2Length = (voice_data_size + WAV_HEADER_SIZE + 4) * 4/3;  // 4/3 is from base64 encoding ratio
   String ContentLength = String(HttpBody1.length() + httpBody2Length + HttpBody3.length()); 
   // String GoogleApiKey = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_key");
   // String GoogleApiServer = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_server");
   String HttpHeader = String("POST /v1/speech:recognize?key=") + sttApiKey +
         String(" HTTP/1.1\r\nHost: ") + sttApiServer + 
         String("\r\nContent-Type: application/json\r\nContent-Length: ") + 
         ContentLength + String("\r\n\r\n");

   /**
    * @brief Open voice file for reading, encode the data, and send to transcribe server
    */
   if(!sdcard.fileOpen(filepath, FILE_READ, false)) {   // exit if can't open file
      free(chunkBufr);                    // free local chunk memory
      free(encodeBufr);
      Serial.printf("Error: Can't open file: %s\n", filepath);
      return ret;
   }

   /**
    * @brief Print header and body 1 strings
    */
   sttClient.print(HttpHeader);
   sttClient.print(HttpBody1);

   /**
    * @brief Encode the WAV header and send to transcribe server
    */
   bytes_rcvd = sdcard.fileRead(chunkBufr, WAV_HEADER_SIZE + 4, 0); 
   encode_base64((uint8_t *)chunkBufr, WAV_HEADER_SIZE + 4, (uint8_t *)encodeBufr);
   sttClient.print((const char *)encodeBufr);   

   /**
    * @brief Loop to encode and send voice data to the transcribe service
    */
   abs_idx = WAV_HEADER_SIZE + 4;   // start of data in file
   while(true) {
      // Encode the mic data with base64 and send to server
      if(sttClient.connected()) {
         bytes_rcvd = sdcard.fileRead(chunkBufr, STT_CHUNK_SIZE, abs_idx);      
         if(bytes_rcvd == 0)              // exit if no more data to read
            break;    

         abs_idx += bytes_rcvd;           // seek to next chunk in file

         // Add base64 encoding to audio chunk and send to google stt service
         uint32_t blen = encode_base64((uint8_t *)chunkBufr, bytes_rcvd, (uint8_t *)encodeBufr);
         data_sent = sttClient.print((const char *)encodeBufr);            
         if(data_sent <= 0) {             // exit if no more data to be sent
            break;
         }  
      } else 
         break; 
   } 
   sdcard.fileClose();                 // close the file opened above

   // Voice data sent, send the remainder of the http body
   sttClient.print(HttpBody3);     
   
   /**
    * @brief Wait for response from STT service
    */
   String STT_Response = "";     
   timeout = millis();

   while (!sttClient.available() && (millis() - timeout) < 5000) { vTaskDelay(10); }
   if( (millis() - timeout) >= 5000 ) {
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

   ret = true; 
   free(chunkBufr);                       // free local chunk memory in PSRAM
   free(encodeBufr);
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
   // empty
}


/********************************************************************
*  @brief Text To Speech class destructor
*/
TEXT_TO_SPEECH::~TEXT_TO_SPEECH() {
   // empty
}


/********************************************************************
 * @brief Load credentials for text to speech service.
 */
bool TEXT_TO_SPEECH::init(void)
{
   ttsApiKey = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_key");
   ttsApiServer = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_server");
   ttsApiEndPoint = sdcard.readJSONfile(SYS_CRED_FILENAME, "google", "api_endpoint");
   if(ttsApiKey.length() == 0 || ttsApiServer.length() == 0 || ttsApiEndPoint.length() == 0)
      return false;
   return true;   
}


/********************************************************************
*  @brief Google text to speech service. Caller must have an API key (same 
*  as the speech to text service.)
*  NOTE: System must be connected to the internet.
*/
bool TEXT_TO_SPEECH::transcribeTextToSpeech(String text, bool play_audio, const char *filepath, int16_t volume) 
{   
   size_t bytesReceived;
   uint16_t chunkCntr = 0;
   char *acptr;
   uint32_t dec_length;
   uint32_t i, offset, mod_len;
   uint32_t idx = 0;
   uint32_t tmo;
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
      sdcard.deleteFile(filepath);

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

   /**
   *  @brief Send JSON request body to google TTS service
   */
   JsonDocument doc;
   doc["input"]["text"] = text;
   doc["voice"]["languageCode"] = TTS_LANGUAGE;
   doc["voice"]["name"] = TTS_VOICE;  //"en-US-Wavenet-B";     // -A or -B is male voice, -C is female
   doc["audioConfig"]["audioEncoding"] = "LINEAR16";
   doc["audioConfig"]["sampleRateHertz"] = AUDIO_SAMPLE_RATE;
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
            audio.playRawAudio((uint8_t *)slabTable[slabCount].ptr+WAV_HDR_SIZE, 
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
            audio.playRawAudio((uint8_t *)slabTable[slabCount].ptr, slabTable[slabCount].used, volume);     
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
   if(filepath) 
      file_is_open = sdcard.fileOpen(filepath, FILE_APPEND, true);

   for (i = 0; i < slabCount; i++) {
      if (slabTable[i].ptr) {
         if(file_is_open) {
            sdcard.fileWrite(slabTable[i].ptr, slabTable[i].used);
         }
         heap_caps_free(slabTable[i].ptr);   // free the memory buffer
         slabTable[i].ptr = nullptr;       
      }
   }
   if(file_is_open) 
      sdcard.fileClose();        // close the file if previously opened

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
   ChatGPTApiKey = sdcard.readJSONfile(SYS_CRED_FILENAME, "openai", "api_key");
   ChatGPTEndPoint = sdcard.readJSONfile(SYS_CRED_FILENAME, "openai", "api_endpoint");
   ChatGPTVersion = sdcard.readJSONfile(SYS_CRED_FILENAME, "openai", "chat_version");

   if(ChatGPTApiKey.length() == 0 || ChatGPTEndPoint.length() == 0 || ChatGPTVersion.length())
      return false;
   else
      return true;
}


/********************************************************************
*  @brief Send text request to chat GPT 
*/
bool CHAT_GPT::chatRequest(const char *req_text, bool enab_tts, uint16_t volume)
{
   uint16_t i;

   if(ChatGPTApiKey.length() == 0) {
      Serial.println("Error: Can't access OpenAI API Key!");
      return false;
   }

   String Request = req_text;
   String Response = sendToChatGPT(Request);
         // Serial.println("Chat response:");
         // Serial.println(Response);
   JsonDocument chat_doc;
   DeserializationError error = deserializeJson(chat_doc, Response);
   if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return false;
   } else {
      String content = chat_doc["choices"][0]["message"]["content"];
      String gpt_model = chat_doc["model"];
      Serial.print("GPT model: "); Serial.println(gpt_model);
      if(content.length() < 1) {
         content = "No response from Chat-GPT!";
      }
      for(i=0; i<content.length(); i++) {
         if(content.charAt(i) == 92 || content.charAt(i) == 34)   // remove '\' or '"' chars
            content.setCharAt(i, ' ');
      }      
      setChatResponseText(content);  
               
      // textToSpeech((VC_AUDIO *)vcAudio, content, volume);  // chat response text converted back to speech                              
   }
   return true;
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

      // Serial.print("POST URL: ");
      // Serial.println(ChatGPTEndPoint);
      // Serial.print("Payload: ");
      // Serial.println(payload);

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