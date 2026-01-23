#include "speechServices.h"
#include "base64.hpp" // included here because code is wrapped in an .hpp file

// Enable speech class objects
#if defined(USE_CHAT_GPT_SERVICE)
CHAT_GPT chatGPT;
#endif

#if defined(USE_SPEECH_TO_TEXT_SERVICE)
SPEECH_TO_TEXT speechToText;
#endif

#if defined(USE_TEXT_TO_SPEECH_SERVICE)
TEXT_TO_SPEECH textToSpeech;
#endif

#if defined(USE_LANGUAGE_TRANSLATE_SERVICE)
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
   translateSourceText = nullptr;
   translateResultsText = nullptr;
}

/********************************************************************
 *  @brief Language Translate  destructor
 */
LANGUAGE_TRANSLATE::~LANGUAGE_TRANSLATE()
{
   heap_caps_free(translateSourceText);
   heap_caps_free(translateResultsText);
}

/********************************************************************
 * @brief Intialize translate credentials.
 */
bool LANGUAGE_TRANSLATE::init(void)
{
   translateSourceText = (char *)heap_caps_malloc(TRANSLATE_TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   if (!translateSourceText)
      return false;
   translateResultsText = (char *)heap_caps_malloc(TRANSLATE_TEXT_BUFR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   if (!translateResultsText)
      return false;
   // read google API credentials
   langApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   langApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   langApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_translate_endpoint");
   if (langApiKey.length() == 0 || langApiServer.length() == 0 || langApiEndPoint.length() == 0)
      return false;
   return true;
}

/********************************************************************
 * @brief Run translation.
 * Source text is in 'translateSourceText'.
 * Output text will be in 'translateResultsText'.
 *
 * @param: lang_from - Google language code <from>. Example: "en-US" (english US)
 * @param: lang_to - Google language code <to>. Example: "th-TH" (Thai)
 */
bool LANGUAGE_TRANSLATE::translate(char *lang_from, char *lang_to)
{
   // Sanity check on buffers and credentials
   if (langApiEndPoint.isEmpty() || langApiKey.isEmpty() ||
       translateSourceText == NULL || translateResultsText == NULL)
      return false;

   HTTPClient http;
   String url = langApiEndPoint + langApiKey;

   http.begin(url);
   http.addHeader("Content-Type", "application/json");

   JsonDocument doc;
   doc["q"] = translateSourceText; // caller placed text to translate here
   doc["target"] = lang_to;        // target language code
   doc["source"] = lang_from;      // source language code (optional; auto-detect if omitted)
   doc["format"] = "text";         // <<— IMPORTANT: avoid HTML entities in output

   String body;
   serializeJson(doc, body);

   int httpCode = http.POST(body);
   if (httpCode <= 0)
   {
      Serial.printf("HTTP error: %s\n", http.errorToString(httpCode).c_str());
      http.end();
      return false;
   }

   String payload = http.getString();
   http.end();
   // Serial.printf("payload=%s\n", payload.c_str());

   JsonDocument resp;
   if (deserializeJson(resp, payload))
   {
      Serial.println("JSON parse failed");
      return false;
   }

   // Safely copy results into PSRAM (null-terminated, truncated if too long)
   JsonVariantConst v = resp["data"]["translations"][0]["translatedText"];
   const char *src = v | ""; // default to "" if 'v' is null/missing
   strlcpy(translateResultsText, src, TRANSLATE_TEXT_BUFR_SIZE);
   return true;
}

/*###################################################################
 *                      Speech To Text class
 ##################################################################*/

#if defined(USE_SPEECH_TO_TEXT_SERVICE)
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
   // empty
}

/********************************************************************
 * @brief Load Speech To Text credentials from config file on SD Card
 * @return true if all creds are loaded successfully.
 */
bool SPEECH_TO_TEXT::init(void)
{
   sttApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   sttApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   sttApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_stt_endpoint");
   if (sttApiKey.length() == 0 || sttApiServer.length() == 0 || sttApiEndPoint.length() == 0)
      return false;

   return true;
}

/********************************************************************
 *  @fn Reconnect stt client after timeout or random disconnect
 */
bool SPEECH_TO_TEXT::clientReconnect(void)
{
   return true;
}

/********************************************************************
 *  @fn Return stt client timeout in seconds
 */
uint32_t SPEECH_TO_TEXT::getClientTimeout(void)
{
   // return sttClient.getTimeout();
   return 0;
}

/********************************************************************
 *  @fn Set sttClient connection timeout in milliseconds
 */
void SPEECH_TO_TEXT::setClientTimeout(uint32_t timeout_ms)
{
   // sttClient.setTimeout(timeout_ms);
}

/********************************************************************
 *  @fn Return client connection status
 *  @return true if client is connected to server, false otherwise
 */
bool SPEECH_TO_TEXT::isClientConnected(void)
{
   return true; // sttClient.connected();
}

/********************************************************************
 * @brief Convert voice audio to a text string. Connect to Google
 *    Speech to Text 'Transcribe' service and send encoded WAV file.
 *    Text will be returned.
 * REQUIRED: System must be connected to the internet & caller has valid API key.
 * @param mode - SOURCE_FROM_FILE = voice audio is sourced from a file on
 *    the SD card. See 'in_file' below.
 *    - SOURCE_FROM_STREAM = audio is sourced from "audio recorder" (see audio.h)
 * @param in_file - file path/name of voice audio file (mode == SOURCE_FROM_FILE)
 * @param out_File - name of file to save audio stream (mode == SOURCE_FROM_STREAM).
 * @param lang_code - google language code of input speech (ex: "en-US").
 */
bool SPEECH_TO_TEXT::convert(uint16_t mode, const char *in_file, 
            const char *out_file, const char *lang_code)
{
   uint32_t timeout;
   uint32_t overhead;
   uint32_t raw_chunk_size = 12288;     // size of chunk data read from file
   uint32_t encoded_chunk_size = 16384; // size of data after encoding (4/3 ratio)
   uint32_t bytes_per_frame = (DEFAULT_SAMPLES_PER_FRAME * 2);
   uint32_t audio_rec_num_frames = raw_chunk_size / bytes_per_frame;
   int32_t voice_data_size;
   uint32_t encode_len;
   bool ret = false;
   char temp;
   uint32_t data_sent, audio_file_index, i;
   int32_t bytes_rcvd = 0;
   uint16_t frame_count = 0;
   rec_status_t *rec_status;
   uint16_t gated_frame_state = REC_STATE_NONE;
   File _file_in, _file_out;
   uint32_t out_file_size = 0;
   B64Stream b64;

   sttResponse.clear(); // clear out response String

   // Can't do this without wifi connected!
   if (WiFi.status() != WL_CONNECTED)
   {
      return false;
   }

   /**
    * @brief Connect to google transcribe server
    */
   WiFiClientSecure sttClient; // create client
   sttClient.setTimeout(30000);
#if defined(USE_CA_CERTIFICATE)
   const char *ca_cert = {CERT_CA_STRING};
   sttClient.setCACert(ca_cert); // more secure connection
#else
   sttClient.setInsecure(); // connect without ca cert
#endif
   // Try to connect to google server
   if (sttClient.connect(sttApiServer.c_str(), 443, 500) <= 0)
   {
      Serial.printf("sttClient Connection failed, server = %s\n", sttApiServer.c_str());
      return false;
   }

   /**
    * @brief Create buffers for reading and encoding voice data
    */
   uint8_t *frameBufr = (uint8_t *)heap_caps_malloc(bytes_per_frame + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   if (!frameBufr)
   {
      sttClient.stop();
      return false;
   }

   uint8_t *chunkBufr = (uint8_t *)heap_caps_malloc(raw_chunk_size + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   if (!chunkBufr)
   {
      heap_caps_free(frameBufr);
      sttClient.stop();
      return false;
   }

   uint8_t *encodeBufr = (uint8_t *)heap_caps_malloc(encoded_chunk_size + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   if (!encodeBufr)
   {
      heap_caps_free(frameBufr);
      heap_caps_free(chunkBufr);
      sttClient.stop();
      return false;
   }

   /**
    * @brief Open WAV in_file for reading (if it exists).
    */
   if (in_file && mode == SOURCE_FROM_FILE) {
      _file_in = sd.fopen(in_file, FILE_READ, false);
      if (!_file_in)
      {
         heap_caps_free(frameBufr);
         heap_caps_free(chunkBufr); // free local chunk memory
         heap_caps_free(encodeBufr);
         Serial.printf("Error: Can't open file: %s\n", in_file);
         sttClient.stop();
         return false;
      } else {
         audio_file_index = WAV_HEADER_SIZE; // start of data in file
      }
   } 

   /**
    * @brief Get voice data from the audio recorder background task.
    */
   if(mode == SOURCE_FROM_STREAM) {
      if(out_file) {                      // save stream audio to a file?
         sd.fremove(out_file);            // delete file if it's already here
         _file_out = sd.fopen(out_file, FILE_APPEND, true); // open new stream output file
      }
      // Start recorder, infinite duration, VAD enabled, use LP filter, send frames to temp buffer
      audio.startRecording(REC_MODE_RECORD, 0.0, ENAB_VAD, ENAB_LP_FILTER, nullptr,
            (int16_t *)frameBufr, 0, DEFAULT_SAMPLES_PER_FRAME);
   }

   /**
    * @brief Build the HTTP header lines and output to the client.
    * @note Uses chunked transfer method.
    */
   sttClient.print("POST /v1/speech:recognize?key=");
   sttClient.print(sttApiKey);
   sttClient.print(" HTTP/1.1\r\nHost: ");
   sttClient.print(sttApiServer); //.c_str());
   sttClient.print("\r\nContent-Type: application/json\r\n");
   sttClient.print("Transfer-Encoding: chunked\r\n");
   sttClient.print("Connection: close\r\n\r\n");

   // Create the JSON prefix & write to client
   String prefix =
       String("{\"config\":{\"encoding\":\"LINEAR16\",\"sampleRateHertz\":16000,"
              "\"languageCode\":\"") +
       String(lang_code) +
       "\"},\"audio\":{\"content\":\"";

   if (!sendChunk(sttClient, (const uint8_t *)prefix.c_str(), prefix.length()))
   {
      heap_caps_free(frameBufr);
      heap_caps_free(chunkBufr); // free local chunk memory
      heap_caps_free(encodeBufr);
      return false;
   }

   /**
    * @brief Loop to encode and send voice data to the transcribe service
    */
   while (true)
   {
      // Read voice data from file
      if (sttClient.connected())
      {
         timeout = millis();
         // Read chunk from SD card file
         if (_file_in && mode == SOURCE_FROM_FILE) {
            bytes_rcvd = sd.fread(_file_in, chunkBufr, raw_chunk_size, audio_file_index);
            audio_file_index += bytes_rcvd; // seek pointer to next chunk in file
            if (bytes_rcvd <= 0)            // exit forever loop if no more data to read
               break;
         } 
         if(mode == SOURCE_FROM_STREAM) {                           
            // Read chunk from audio recorder (real time data)
            bytes_rcvd = 0;
            frame_count = 0;
            while ((millis() - timeout < 2000)) {   // failsafe timeout 2 secs
               // check status from gated frame queue
               rec_status = audio.getRecordingStatus();
               // Recording complete?
               if ((rec_status->state & REC_STATE_REC_CMPLT) > 0) { // recording completed?
                  break;
               }

               // Frame available, concatenate 'n' frames into chunk buffer
               if ((rec_status->state & REC_STATE_FRAME_AVAIL) > 0) {
                  memcpy(chunkBufr + (frame_count * bytes_per_frame), frameBufr, bytes_per_frame);
                  bytes_rcvd += bytes_per_frame;
                  if (++frame_count >= audio_rec_num_frames) { // 'n' frames accumulated? 
                     break;            // exit the inside timeout loop
                  }
               }
               vTaskDelay(1);
            }
            if ((rec_status->state & REC_STATE_REC_CMPLT) > 0) { // all done? if so, break from outer main loop
                  Serial.println("\neor");
               break;
            }
         }
         // Encode the voice data with base64 and send to server
         if((millis() - timeout) < 2000) {   // ignore encode if timeout
            out_file_size += bytes_rcvd;     // keep total for file
            // Write stream to file
            if(mode == SOURCE_FROM_STREAM && _file_out) {
               sd.fwrite(_file_out, chunkBufr, bytes_rcvd);
            }
            // Use chat gpt base64 encoder
            encode_len = b64.encode(chunkBufr, bytes_rcvd, (char *)encodeBufr, encoded_chunk_size);    
            // Serial.printf("chunk sz=%d, bytes rcvd=%d, enc len=%d\n", encoded_chunk_size, bytes_rcvd, encode_len);
            if (encode_len > 0) {
               if (!sendChunk(sttClient, (const uint8_t *)encodeBufr, encode_len)) {
                  heap_caps_free(frameBufr);
                  heap_caps_free(chunkBufr); // free local chunk memory
                  heap_caps_free(encodeBufr);
                  return false;
               }
            }
         } else {
            Serial.printf("timeout before encode! frame count=%d\n", frame_count);
         }
      }
      else
      {
         Serial.println(F("Error: STT client lost connection during writing!"));
         break;
      }
   }
   if(_file_in)
      sd.fclose(_file_in);                // close the input file if opened previously

   if(_file_out) {    
      sd.fclose(_file_out);               // close stream output file if opened previously

      // Add WAV header now that we have the correct data size
      _file_out = sd.fopen(out_file, FILE_READ, false); // open data file for reading
      File _file_temp = sd.fopen("/temp_copy.wav", FILE_APPEND, true); // copy to the 'temp' file           
      if(_file_out && _file_temp) {
         // Create the WAV header with correct data size        
         memset(chunkBufr, 0x0, WAV_HEADER_SIZE);
         audio.CreateWavHeader((uint8_t *)chunkBufr, out_file_size);
               Serial.printf("out sz=%d\n", out_file_size);
               sys_utils.hexDump((uint8_t *)chunkBufr, 64);

         // Write the WAV header to the temp file
         bool file_ok = sd.fwrite(_file_temp, chunkBufr, WAV_HEADER_SIZE);

         // Copy the voice data from the output file to the temp file
         uint32_t frame_indx = 0;
         while(file_ok) {
            int32_t bytes_read = sd.fread(_file_out, chunkBufr, bytes_per_frame, frame_indx);
            if(bytes_read == 0)
               break;
            frame_indx += bytes_read;
            if(!sd.fwrite(_file_temp, chunkBufr, bytes_read))
               break;
         }
         // close the files and delete the temporary file
         sd.fclose(_file_out);
         sd.fclose(_file_temp);
         sd.fremove(out_file);         // delete original file  
         sd.frename("/temp_copy.wav", out_file);   // then name temp file as original file
      }
   }
     
   // Stop audio recorder (assuming it's running)
   if(mode == SOURCE_FROM_STREAM)
      audio.stopRecording(); 

   // Flush base64 padding
   size_t pad = b64.flush((char *)encodeBufr, encoded_chunk_size);
   if (pad > 0)
   {
      if (!sendChunk(sttClient, (const uint8_t *)encodeBufr, pad))
      {
         heap_caps_free(frameBufr);
         heap_caps_free(chunkBufr); // free local chunk memory
         heap_caps_free(encodeBufr);
         return false;
      }
   }

   // Send JSON suffix - close the JSON body
   const char *suffix = "\"}}";
   if (!sendChunk(sttClient, (const uint8_t *)suffix, strlen(suffix)))
   {
      heap_caps_free(frameBufr);
      heap_caps_free(chunkBufr); // free local chunk memory
      heap_caps_free(encodeBufr);
      return false;
   }

   // Terminate chunked transfers
   if (!endChunks(sttClient))
   {
      heap_caps_free(frameBufr);
      heap_caps_free(chunkBufr); // free local chunk memory
      heap_caps_free(encodeBufr);
      return false;
   }

   /**
    * @brief Wait for response from STT service
    */
   String STT_Response = "";
   timeout = millis();
   while (!sttClient.available() && (millis() - timeout) < 10000)
   {
      vTaskDelay(10);
   }

   // Exit on timeout
   if ((millis() - timeout) >= 10000)
   {
      heap_caps_free(frameBufr);
      heap_caps_free(chunkBufr); // free local chunk memory
      heap_caps_free(encodeBufr);
      sttResponse = "Client response timeout";
      sttClient.stop();
      return ret;
   }

   // Read JSON text response data into String object
   while (sttClient.available())
   {
      char temp = sttClient.read();
      STT_Response = STT_Response + temp;
   }
   // Serial.printf("STT text: %s\n", STT_Response.c_str());
   /**
    * @brief Extract text response from JSON document
    */
   JsonDocument doc;

   // Ignore text up to the start of the json document
   int16_t position = STT_Response.indexOf('{'); // json doc starts with first '{'
   sttResponse = STT_Response.substring(position);

   // deserialize json format so we can search for text segments
   DeserializationError error = deserializeJson(doc, sttResponse);
   if (error)
   {
      Serial.print(F("STT deserializeJson() failed: "));
      Serial.println(error.f_str());
      sttResponse = "";
   }
   else
   {
      JsonArray results = doc["results"].as<JsonArray>();
      String fullTranscript;

      // Iterate through all possible "alternatives" and concatenate the results.
      for (JsonObject result : results)
      {
         JsonArray alternatives = result["alternatives"].as<JsonArray>();

         // Pick the best (first) alternative
         if (!alternatives.isNull() && alternatives.size() > 0)
         {
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
   sttClient.stop();
   heap_caps_free(frameBufr);
   heap_caps_free(chunkBufr); // free local chunk memory in PSRAM
   heap_caps_free(encodeBufr);
   return (!sttResponse.isEmpty()); // return true if response has text
}


/********************************************************************
 * @brief Helper for STT - send chunked data
 */
bool SPEECH_TO_TEXT::sendChunk(WiFiClientSecure &client, const uint8_t *data, size_t len)
{
   if (len == 0)
      return true;
   char hexlen[10];
   int32_t bytes_written = 0;
   int32_t bytes_total = len;
   uint32_t tmo = millis();

   int32_t n = snprintf(hexlen, sizeof(hexlen), "%04X\r\n", (unsigned)len);
   if (client.write((const uint8_t *)hexlen, n) != n)
      return false;
   // Manage multiple writes if client can't complete full length
   while (bytes_total > 0)
   {
      bytes_written = client.write(data, bytes_total);
      bytes_total -= bytes_written;
      if ((millis() - tmo) > 2000)
         break; // failsafe exit if client times out
   }
   if (client.write((const uint8_t *)"\r\n", 2) != 2)
      return false;
   return true;
}


/********************************************************************
 * @brief Helper for STT - end chunking 
 */
bool SPEECH_TO_TEXT::endChunks(WiFiClientSecure &client)
{
   return client.write((const uint8_t *)"0\r\n\r\n", 5) == 5;
}


/**
 * @brief Debug client disconnects
 */
bool SPEECH_TO_TEXT::debugClient(uint8_t inum, uint16_t data_sent, uint16_t enc_len)
{
   // Check to see if an error occurred on the file header (first 64 bytes)
   // Serial.printf("*** Client disconnected! inum=%d, data_sent=%d, encode_len=%d\nResponse: ",
   //       inum, data_sent, enc_len);
   // while (sttClient.available()) {
   //    int c = sttClient.read();
   //    if (c < 0) break;
   //    Serial.write(c);
   // }
   //    Serial.println("");
   // sttClient.stop();
   // return false;

   return true;
}
#endif

/*###################################################################
 *                      Text To Speech class
 ##################################################################*/

#if defined(USE_TEXT_TO_SPEECH_SERVICE)
/********************************************************************
 *  @brief Text To Speech class constructor
 */
TEXT_TO_SPEECH::TEXT_TO_SPEECH()
{
   // empty constructor
}

/********************************************************************
 *  @brief Text To Speech class destructor
 */
TEXT_TO_SPEECH::~TEXT_TO_SPEECH()
{
   // empty destructor
}

/********************************************************************
 * @brief Load credentials for text to speech service.
 */
bool TEXT_TO_SPEECH::init(void)
{
   ttsApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_key");
   ttsApiServer = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_server");
   ttsApiEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "google", "api_tts_endpoint");
   if (ttsApiKey.length() == 0 || ttsApiServer.length() == 0 || ttsApiEndPoint.length() == 0)
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
bool TEXT_TO_SPEECH::convert(char *text, bool play_audio, const char *filepath,
                                            int16_t volume, const char *lang_code, const char *voice_name, float speak_rate)
{
   size_t bytesReceived;
   uint16_t chunkCntr = 0;
   char *acptr;
   uint32_t total_bytes = 0;
   uint32_t i;
   uint32_t tmo;
   File _file;
   bool file_is_open = false;
   audio_play_t audio_play;
#define WAV_HDR_SIZE 44
#define MAX_SLABS 1024

   // ---- Slab entry (lives in PSRAM) ----
   struct SlabEntry
   {
      uint8_t *ptr;  // pointer to buffer in PSRAM
      uint32_t used; // actual used size
   };
   // ---- Slab table (lives in PSRAM) ----
   SlabEntry *slabTable = nullptr;
   // uint32_t slabCount = 0;
   char *p;

   // delete old duplicate file if it exists
   if (filepath)
      sd.fremove(filepath);

   /**
    *  @brief Remove unsupported escape sequences from text
    */
   for (i = 0; i < strlen(text); i++)
   {
      if (text[i] == 92 || text[i] == 34) // remove '\' or '"' chars
         text[i] = ' ';
   }

   /**
    * @brief Create an array of pointers to 'slab buffers'. 1 second of audio will
    * use approx 16 slab buffers, therefore 1 minute of audio will be approx 960 slab buffers
    */
   slabTable = (SlabEntry *)heap_caps_malloc(MAX_SLABS * sizeof(SlabEntry),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
   if (!slabTable)
   {
      Serial.println(F("Error: Failed to allocate slab table in PSRAM!"));
      return false;
   }

   /**
    *  @brief Create response buffer
    */
   char *response_bufr = (char *)heap_caps_malloc(TTS_CHUNK_SIZE + 64, MALLOC_CAP_SPIRAM);
   if (!response_bufr)
   {
      Serial.println(F("Error: No memory for response_bufr!"));
      return false;
   }

   /**
    *  @brief initialize wifi & http clients
    */
   static WiFiClientSecure ttsClient;
   ttsClient.setInsecure(); // set secure client insecure!
   static HTTPClient httpClient;
   httpClient.useHTTP10(true); // IMPORTANT: Use older HTTP 1.0 to get continuous response data
   // concatenate google endpoint and api key into one string
   String GoogleApiKey = ttsApiEndPoint + ttsApiKey;
   httpClient.begin(ttsClient, GoogleApiKey);

   /**
    *  @brief Send JSON request body to google TTS service
    */
   JsonDocument doc;
   doc["input"]["text"] = text;
   doc["voice"]["languageCode"] = lang_code; // TTS_LANGUAGE;
   if (!voice_name)
      doc["voice"]["name"] = LANG_VOICE_MALE; // default english voice "en-US-Wavenet-G" female voice
   else
      doc["voice"]["name"] = voice_name; // Example: "en-US-Wavenet-G" female voice
   doc["audioConfig"]["audioEncoding"] = "LINEAR16";
   doc["audioConfig"]["sampleRateHertz"] = 16000;
   doc["audioConfig"]["speakingRate"] = speak_rate; // 0.0-1.0. If talking too fast - slow it down!
   String requestBody;
   serializeJson(doc, requestBody); // convert json to a String object
   httpClient.addHeader("Content-Type", "application/json");
   int httpCode = httpClient.POST(requestBody);

   if (play_audio)
   {
      i2s_zero_dma_buffer(I2S_SPEAKER); // prevent glitches
      audio_play.cmd = PLAY_CLEAR;
      audio_play.chunk_bytes = TTS_DEC_FILE_SIZE;
      audio_play.chunk_depth = 4;
      audio_play.pChunk = nullptr;
      audio_play.bytes_to_write = 0;
      audio_play.volume = sys_utils.getVolume();
      xQueueSend(qAudioPlay, &audio_play, 200);
   }

   /**
    *  @brief Get audio response data from the google TTS service.
    *  @note The response is a WAV file with a typical 44 byte header.
    */
   while (httpClient.connected())
   {
      /**
       *  @brief First chunk in json format contains the marker "audioContent"
       */
      if (chunkCntr == 0)
      { // process first chunk
         bytesReceived = httpClient.getStream().readBytes((char *)response_bufr, TTS_CHUNK_SIZE + TTS_PREAMBLE_LEN);
         if (bytesReceived <= TTS_PREAMBLE_LEN)
         { // exit if no audio data to read
            break;
         }
         // acptr = (char *)response_bufr + TTS_PREAMBLE_LEN;

         // Save decoded audio data in PSRAM buffer array (slabTable).
         slabTable[chunkCntr].ptr = (uint8_t *)heap_caps_malloc(TTS_DEC_FILE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
         memset(slabTable[chunkCntr].ptr, 0, TTS_DEC_FILE_SIZE); // clear new memory
         slabTable[chunkCntr].used = decode_base64((unsigned char *)response_bufr + TTS_PREAMBLE_LEN,
                                                   bytesReceived - TTS_PREAMBLE_LEN,
                                                   (unsigned char *)slabTable[chunkCntr].ptr);

         // Serial.printf("brcv=%d, bytesRcvd=%d, offset=%d, mod_len=%d, slab.used=%d\n", brcv, bytesReceived, offset, mod_len, slabTable[slabCount].used);
         //    sys_utils.hexDump((uint8_t *)slabTable[chunkCntr].ptr + WAV_HDR_SIZE, 128); //=====================

         /**
          * If play audio, play first chunk from the slabTable
          */
         if (play_audio)
         {
            audio_play.cmd = PLAY_AUDIO;
            audio_play.chunk_bytes = TTS_DEC_FILE_SIZE;                              // maximum bytes in a chunk
            audio_play.chunk_depth = 4;                                              // # chunks to allocate
            audio_play.pChunk = (uint16_t *)slabTable[chunkCntr].ptr + WAV_HDR_SIZE; // step past WAV header
            audio_play.bytes_to_write = slabTable[chunkCntr].used - WAV_HDR_SIZE;    // len - WAV header
            audio_play.volume = sys_utils.getVolume();
            xQueueSend(qAudioPlay, &audio_play, 200);
         }
         // increment to next buffer
         // slabCount = 1;
      }
      else
      { // Additional chunks are data only
         bytesReceived = httpClient.getStream().readBytes(response_bufr, TTS_CHUNK_SIZE);
         if (bytesReceived > 0)
         {
            // Add new PSRAM buffer to the slab array
            slabTable[chunkCntr].ptr = (uint8_t *)heap_caps_malloc(TTS_DEC_FILE_SIZE, MALLOC_CAP_SPIRAM);
            memset(slabTable[chunkCntr].ptr, 0, TTS_DEC_FILE_SIZE); // clear new memory
            slabTable[chunkCntr].used = decode_base64((unsigned char *)response_bufr, bytesReceived,
                                                      (unsigned char *)slabTable[chunkCntr].ptr);

            // Play remaining chunks from the slabTable
            if (play_audio)
            {
               audio_play.cmd = PLAY_AUDIO;
               audio_play.chunk_bytes = TTS_DEC_FILE_SIZE;
               audio_play.chunk_depth = 4;
               audio_play.pChunk = (uint16_t *)slabTable[chunkCntr].ptr;
               audio_play.bytes_to_write = slabTable[chunkCntr].used;
               audio_play.volume = sys_utils.getVolume();
               xQueueSend(qAudioPlay, &audio_play, 200);
            }
         }
         else
         {
            break;
         }
      }
      chunkCntr++;
   }

   httpClient.end();
   if (httpCode != 200) // code 200 = success, all others bad!
      Serial.printf("Error: TTS http client, code=%d\n", httpCode);

   /**
    * Write the collective buffers to a file on the SD Card if 'filepath'.
    * PSRAM slab memory is also free'd here.
    */
   if (filepath)
   {
      _file = sd.fopen(filepath, FILE_APPEND, true);
      file_is_open = (_file);
   }
   for (i = 0; i < chunkCntr; i++)
   { // copy chunks of audio to SD card file
      if (slabTable[i].ptr)
      {
         if (file_is_open)
         {
            sd.fwrite(_file, slabTable[i].ptr, slabTable[i].used);
         }
      }
   }
   if (file_is_open)
      sd.fclose(_file); // close the file if previously opened

   // free the individual slabTable chunk memory
   for (i = 0; i < chunkCntr; i++)
   {
      heap_caps_free(slabTable[i].ptr); // free the memory buffer
      slabTable[i].ptr = nullptr;
   }
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
#if defined(USE_CHAT_GPT_SERVICE)

/********************************************************************
 *  @brief CHAT_GPT constructor
 */
CHAT_GPT::CHAT_GPT()
{
   // empty
}

/********************************************************************
 *  @brief CHAT_GPT destructor
 */
CHAT_GPT::~CHAT_GPT()
{
   // empty
}

/********************************************************************
 *  @fn Initialize ChatGPT credentials (API key, endpoint, chat version)
 */
bool CHAT_GPT::init(void)
{
   ChatGPTApiKey = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "api_key");
   ChatGPTEndPoint = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "api_endpoint");
   ChatGPTVersion = sd.readJsonFile(SYS_CRED_FILENAME, "openai", "chat_version");

   if (ChatGPTApiKey.length() == 0 || ChatGPTEndPoint.length() == 0 || ChatGPTVersion.length() == 0)
      return false;
   else
      return true;
}

/********************************************************************
 *  @brief Send text request to chat GPT. Response text is saved in
 *  the String object 'chat_response'.
 */
bool CHAT_GPT::request(const char *req_text)
{
   uint16_t i;
   String outerStr;

   if (ChatGPTApiKey.length() == 0)
   {
      Serial.println(F("Error: Missing OpenAI API Key!"));
      return false;
   }

   // Clear all response Strings
   chat_response.clear();
   intent.clear();
   device.clear();
   action.clear();

   String Request = req_text;
   String Response = sendToChatGPT(Request);
   // Serial.printf("CHAT RESPONSE: %s\n", Response.c_str()); //###########################

   JsonDocument chat_doc;
   DeserializationError error = deserializeJson(chat_doc, Response);
   if (error)
   {
      Serial.println(F("DeserializeJson() failed!"));
      return false;
   }
   else
   {
      outerStr = chat_doc["choices"][0]["message"]["content"].as<String>();
      JsonDocument innerDoc;
      DeserializationError des_err = deserializeJson(innerDoc, outerStr);
      if (!des_err)
      {
         response_mode = innerDoc["mode"].as<String>();
         chat_response = innerDoc["reply"].as<String>();
         gpt_model = chat_doc["model"].as<String>();
         if (response_mode.equalsIgnoreCase("command"))
         {
            intent = innerDoc["intent"].as<String>();
            device = innerDoc["device"].as<String>();
            action = innerDoc["action"].as<String>();
         }
         Serial.printf("gpt model: %s, response=%s, mode=%s\n", gpt_model.c_str(), chat_response.c_str(), response_mode.c_str());
         Serial.printf("intent=%s, device=%s, action=%s\n", intent.c_str(), device.c_str(), action.c_str());
      }
      else
      {
         response_mode = "";
         chat_response = "Error: No response from Chat-GPT!";
         gpt_model = "?";
      }
      // Strip '\' or '"' or '*' chars from text response and replace with space
      for (i = 0; i < chat_response.length(); i++)
      {
         if (chat_response.charAt(i) == 92 || chat_response.charAt(i) == 34 || chat_response.charAt(i) == 42)
            chat_response.setCharAt(i, ' ');
      }
   }
   return true;
}

/********************************************************************
 * @brief Helper function sends 'message' to Chat GPT server.
 */
String CHAT_GPT::sendToChatGPT(const String &message)
{
   if (ChatGPTApiKey.length() == 0 || ChatGPTEndPoint.length() == 0)
   {
      return "ChatGPT Credentials Not Set!";
   }

   String sanitizedMessage = sanitizeString(message);
   String payload = R"({"model": ")" + ChatGPTVersion +
                    R"(", "messages": [{"role": "system", "content": ")" + ChatSystemPrompt +
                    R"("}, {"role": "user", "content": ")" + sanitizedMessage + R"("}]})";
   // Serial.printf("\npayloadA=%s\n\n", payload.c_str());
   return makeHttpRequest(payload);
}

/********************************************************************
 * @brief Make an HTTP request to the Chat GPT server (helper function).
 */
String CHAT_GPT::makeHttpRequest(const String &payload)
{
   HTTPClient http;
   WiFiClientSecure client;
   // Serial.printf("makeHttpReq=%s\n", payload.c_str());

   // Uses insecure connection. Google 'CA Cert' if you need more security.
   client.setInsecure();

   if (!http.begin(client, ChatGPTEndPoint))
   { // initialize & check endpoint
      Serial.println("Error: HTTP begin failed.");
      return "HTTP begin failed.";
   }

   http.setTimeout(10000); // Timeout = 10 seconds if not probably null response
   http.addHeader("Content-Type", "application/json");
   http.addHeader("Accept", "application/json");

   String authHeader = "Bearer " + ChatGPTApiKey;
   // Serial.print("Using Authorization header: ");
   // Serial.println(authHeader);
   http.addHeader("Authorization", authHeader);

   int retryCount = 0;
   int maxRetries = 2;
   int httpResponseCode = -1;
   String response;

   while (retryCount <= maxRetries)
   {
      httpResponseCode = http.POST(payload);
      if (httpResponseCode > 0)
      {
         break;
      }
      else
      {
         Serial.print("HTTP POST failed on try ");
         Serial.print(retryCount + 1);
         Serial.print(". Error: ");
         Serial.println(http.errorToString(httpResponseCode));
         retryCount++;
         delay(1000);
      }
   }

   if (httpResponseCode > 0)
   {
      if (httpResponseCode >= 200 && httpResponseCode < 300)
      {
         response = http.getString();
         // Serial.printf("HTTP Response code: %d\nResponse body: %s\n", httpResponseCode, response.c_str());
      }
      else
      {
         response = "HTTP POST returned code: " + String(httpResponseCode) + ". Response: " + http.getString();
         Serial.println(response);
      }
   }
   else
   {
      response = "HTTP POST failed after retries. Error: " + String(http.errorToString(httpResponseCode).c_str());
      Serial.println(response);
   }

   http.end();
   return response;
}

/********************************************************************
 * @brief Helper function to clean bogus characters from message string
 */
String CHAT_GPT::sanitizeString(const String &input)
{
   String sanitized = input;
   sanitized.replace("\\", "\\\\");
   sanitized.replace("\"", "\\\"");
   sanitized.replace("\n", "\\n");
   sanitized.replace("\r", "\\r");
   sanitized.replace("\t", "\\t");

   for (int i = 0; i < sanitized.length(); i++)
   {
      if (sanitized[i] < 32 && sanitized[i] != '\n' && sanitized[i] != '\r' && sanitized[i] != '\t')
      {
         sanitized.remove(i, 1);
         i--;
      }
   }

   return sanitized;
}
#endif
