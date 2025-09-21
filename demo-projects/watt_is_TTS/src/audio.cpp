/**
 * audio.cpp
 */
#include "audio.h"

AUDIO audio;

// Microphone task queue handle
TaskHandle_t h_taskAudioRec = NULL;          // creat a task handle for mic bg task
QueueHandle_t h_QueueAudioRecCommand;        // queue command handle
QueueHandle_t h_QueueAudioRecStatus;         // queue status handle

// Tone task queu handle
TaskHandle_t h_playTone = NULL;              // creat a task handle for mic bg task
QueueHandle_t h_QueueToneTask;               // queue command handle


AUDIO::AUDIO(void)
{
   playWavFromSD_Stop = false;
   playWavFromSD_Progress = -1;
}

AUDIO::~AUDIO(void)
{
   // empty destructor
}


/********************************************************************
 * @brief Read and discard read dma buffer contents
 */
void AUDIO::clearReadBuffer(void)
{
#define BUFR_DEPTH      (1024 * sizeof(uint16_t) * 2)   // clear two 1K buffers
   size_t bytes_read;
   uint8_t *junk_bufr = (uint8_t *)heap_caps_malloc(BUFR_DEPTH, MALLOC_CAP_SPIRAM);  
   i2s_read(I2S_NUM_0, (uint8_t *)junk_bufr, BUFR_DEPTH, &bytes_read, portMAX_DELAY);  

   free(junk_bufr);
}


/********************************************************************
 * @brief Initialize mic & speaker. Install background task to collect 
 * microphone data.
 */
bool AUDIO::init(uint32_t sample_rate)
{
   // Initialize Microphone I2S driver for ICS43434 mems mic
   if(!initMicrophone(sample_rate))
      return false;

   // Initialize Speaker I2S driver for MAX98357 speaker driver
   if(!initSpeaker(sample_rate)) 
      return false;

   /**
    * @brief Start background task to perform microphone data collection.
    */      
   h_QueueAudioRecCommand = xQueueCreate(5, sizeof(rec_command_t));    // commands to recording task
   h_QueueAudioRecStatus = xQueueCreate(5, sizeof(rec_status_t)); // status from recording task

   // Start background audio task running in core 0
   xTaskCreatePinnedToCore(
      taskRecordAudio,                    // Function to implement the task 
      "Recording Task",                   // Name of the task (optional)
      16384,                              // Stack size in words. What should it be? dunno but if it crashes it might be too small!
      NULL,                               // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_taskAudioRec,                    // Task handle. 
      0);                                 // this task runs in Core 0. 

   return true;
}


/********************************************************************
*  @brief Audio recording background task. Runs in core 0.
*/
void taskRecordAudio(void * params)
{
// misc constants
#define MIC_GAIN_FACTOR      20       // useful mic gain for playback

   rec_command_t rec_command;
   rec_command.mode = REC_MODE_IDLE;
   rec_command.filepath = NULL;
   rec_command.data_dest = NULL;
   rec_command.duration_secs = 0;    // default = timed recording disabled
   rec_command.samples_per_frame = 1500;  // default samples/frame
   rec_command.use_lowpass_filter = false;
   rec_command.filter_cutoff_freq = FILTER_CUTOFF_FREQ; // default 3db cutoff

   uint32_t i, j, k;
   int16_t asample; 
   uint16_t recording_stop = 0;
   float fl;
   size_t bytes_read; //, bytes_written;  
   bool file_ok = false;
   bool exec_recording = false;
   bool stop_recording = false;

   uint16_t samples_per_frame = 1024;     // default value
   int16_t *caller_bufr = nullptr;
   bool use_lp_filter = false;

   rec_status_t rec_status;
   rec_status.status = REC_STATUS_NONE;
   uint32_t rec_frame_count;
   uint32_t max_frames;
   uint8_t wav_hdr[WAV_HEADER_SIZE + 4];

   // Internal microphone buffers (one frame)
   uint8_t *mic_raw_data_bufr = (uint8_t *)heap_caps_malloc((MAX_SAMPLES_PER_FRAME * sizeof(int32_t)) + 16, MALLOC_CAP_SPIRAM);  
   uint8_t *mic_sample_bufr = (uint8_t *)heap_caps_malloc((MAX_SAMPLES_PER_FRAME * sizeof(int16_t)) + 16, MALLOC_CAP_SPIRAM);  
   float *mic_input = (float *)heap_caps_malloc((MAX_SAMPLES_PER_FRAME * sizeof(float)) + 16, MALLOC_CAP_SPIRAM);  
   float *mic_output = (float *)heap_caps_malloc((MAX_SAMPLES_PER_FRAME * sizeof(float)) + 16, MALLOC_CAP_SPIRAM);  
   
   /**
    * @brief Generate coefficients for lowpass IIR filter
    */
   #define FILTER_TAPS           6

   // Float IIR coefficients for 2500 Hz cutoff filter
   float filter_coeffs[FILTER_TAPS] = {1, 1, 1, 1, 1, 1};
   float filter_delay_line[2];            // delay line
   float cutoff_freq;

   // i2s_zero_dma_buffer(I2S_NUM_0);        // does this actualy do anything for reads?

   /**
    * @brief Infinite loop to capture frames of mic data. 
    */
   while(true) {

      /**
       * @brief Check receiving command queue for mode changes
       */
      if(xQueueReceive(h_QueueAudioRecCommand, &rec_command, 0) == pdTRUE) {    // check for commands
         if(rec_command.mode == REC_MODE_START) {
            exec_recording = true;

            // copy loop values from queue struct
            use_lp_filter = rec_command.samples_per_frame;
            caller_bufr = rec_command.data_dest;
            use_lp_filter = rec_command.use_lowpass_filter;

            // Convert recording duration to number of frames to record
            // Duration 0.0 results in 1 frame being recorded
            fl = float(samples_per_frame) / float(AUDIO_SAMPLE_RATE);   // one frame time in ms
            max_frames = (int(ceil(rec_command.duration_secs / fl)));      
            if(max_frames == 0) max_frames = 1;
            rec_frame_count = 0;

            // Generate new coefficients for the IIR audio filter. 
            // NOTE: This function is unique to the ESP32-S3 mcu.
            if(use_lp_filter) {
               cutoff_freq = rec_command.filter_cutoff_freq / float(AUDIO_SAMPLE_RATE);
               // NOTE: Qfactor == 0.5 <smoother cutoff rate>, 1.0 <sharper cutoff rate>
               dsps_biquad_gen_lpf_f32((float *)&filter_coeffs, cutoff_freq, 0.707); // nominal cutoff rate
            }

            /**
             * @brief If writing to a file: delete old file, open file, write WAV header to file
             */
            file_ok = false;                 // no output to file       
            if(rec_command.filepath != NULL && strlen(rec_command.filepath) > 0) {       // write audio to file?
               sdcard.deleteFile(rec_command.filepath);  // delete old file if it exists
            
               if(sdcard.fileOpen(rec_command.filepath, FILE_APPEND, true)) {
                  uint32_t file_sz = (max_frames * (rec_command.samples_per_frame * sizeof(int16_t))); // + WAV_HEADER_SIZE;
                  memset(&wav_hdr, 0x0, WAV_HEADER_SIZE + 4);
                  audio.CreateWavHeader((uint8_t *)&wav_hdr, file_sz);
                  file_ok = sdcard.fileWrite((uint8_t *)wav_hdr, WAV_HEADER_SIZE + 4);
               }
            }

         } else if(rec_command.mode == REC_MODE_STOP) {
            rec_command.mode = REC_MODE_IDLE;
            exec_recording = false;
            stop_recording = true;
         } else if(rec_command.mode == REC_MODE_SEND_STATUS) {
            rec_status.max_frames = max_frames;
            rec_status.recorded_frames = rec_frame_count;
            rec_status.status = REC_STATUS_PROGRESS;  // report progress on demand
            xQueueSend( h_QueueAudioRecStatus, ( void * ) &rec_status, ( TickType_t ) 100 ); // notify that text to speech is complete  
         } else if(rec_command.mode == REC_MODE_KILL) { // delete task?
            break;
         } 
      }

      /**
       * Read audio samples from the mic I2S bus
       */
      if(exec_recording) {
         i2s_read(I2S_NUM_0, (uint8_t *)mic_raw_data_bufr, samples_per_frame * sizeof(int32_t), 
               &bytes_read, portMAX_DELAY);  
         uint16_t num_samples = bytes_read / sizeof(int32_t);

         /**
         * @brief Crunch 24 bit mic audio samples into signed 16 bit values 
         */                                   
         for (j = 0; j < num_samples; j++) {    // samples are 24 bit data in 32 bit value
            asample = mic_raw_data_bufr[(j*4)+3];                 // MSB of 24 bit data
            asample = asample << 8 | mic_raw_data_bufr[(j*4)+2];  // LSB "  "    
            asample *= MIC_GAIN_FACTOR;  // higher gain for 'useful' audio volume    

            if(!use_lp_filter) { 
               // normalize int sample and save in caller dest bufr    
               reinterpret_cast<int16_t*>(mic_sample_bufr)[j] = asample;           
            } else {
               mic_input[j] = float(asample);   // save as float data for LP filter
            }
         }

         /**
          * @brief Apply butterworth audio filter to mic data. This uses an optimized DSP 
          *    function that is unique to the ESP32-S3 MCU.
          */
         if(use_lp_filter) {
            dsps_biquad_f32_aes3(mic_input, mic_output, num_samples, 
                  (float *)&filter_coeffs, (float *)&filter_delay_line);

            // Convert float data back into signed 16 bit integer and send to caller bufr
            for(j=0; j<samples_per_frame; j++) {
               reinterpret_cast<int16_t*>(mic_sample_bufr)[j] = int(mic_output[j]);
            }
         }         

         /**
          * @brief Write one frame of mic audio to file on the SD Card
          */
         if(file_ok) {
            sdcard.fileWrite((uint8_t *)mic_sample_bufr, num_samples * sizeof(int16_t));
         }         

         /**
          * @brief Copy 16 bit mic data to callers buffer (16 bit int)
          */
         if(caller_bufr) {
            memcpy((uint8_t *)caller_bufr, (uint8_t *)mic_sample_bufr, num_samples * sizeof(int16_t));
         }
   
         /**
          * @brief Check if recording is complete.
          */
         if(++rec_frame_count >= max_frames) {   // test for completion
            stop_recording = true;
         }
      }    

      /**
       * @brief Stop the current recording and inform caller of completion
       */
      if(stop_recording) {
         stop_recording = false;          // do only once
         exec_recording = false;          // recording stopped
         rec_status.max_frames = max_frames;
         rec_status.recorded_frames = rec_frame_count;         
         // inform caller that recording is completed
         rec_status.status = REC_STATUS_REC_CMPLT;
         xQueueSend( h_QueueAudioRecStatus, ( void * ) &rec_status, ( TickType_t ) 1000 ); // notify that text to speech is complete               
         if(file_ok) {              
            sdcard.fileClose();     // close file that was opened inside this function
         }
      }

      vTaskDelay(1);
   }                 // ***end*** while(true)       

   /**
    * Kill the background task - release used buffer & queue memory
    */ 
   if(file_ok)               
      sdcard.fileClose();                 // close file if it was opened previously
   free(mic_raw_data_bufr);               // free internal buffers in PSRAM
   free(mic_sample_bufr);
   free(mic_input);
   free(mic_output);
   vQueueDelete(h_QueueAudioRecStatus);   // free status queue memory 
   vQueueDelete(h_QueueAudioRecCommand);  // free command queue memory    
   vTaskDelay(10);                        // wait a tad
   vTaskDelete(NULL);                     // remove thy self
}


/********************************************************************
 * @brief Helper function for audio recording to calculate number of 
 * audio frames from duration in seconds.
 * @param duration_secs - number of seconds to record
 * @param samples_frame - number of 16 bit samples in one frame (default=1000)
 * @param sample_rate_hz - default 16000 samples per second
 * @return (integer) equiv number of frames for specified duration.
 */
uint32_t AUDIO::convDurationToFrames(float duration_secs, float samples_frame, float sample_rate_hz)
{
   float fl = (samples_frame / sample_rate_hz );   // one frame time in ms
   return (int(ceil(duration_secs / fl))); 
}


/********************************************************************
 * @brief Check current state of the audio recording background task
 * @return If status, return ptr to a rec_status_t struct. Otherwise
 *    return REC_STATUS_NONE status.
 */
rec_status_t * AUDIO::getRecordingStatus(void)
{
   rec_command_t rec_cmds;
   rec_cmds.mode = REC_MODE_SEND_STATUS;
   static rec_status_t _rec_status;       // access avail after function closes

   // Request status from current recording task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &rec_cmds, ( TickType_t ) 10 );             

   // Wait for response. If none, return no status
   if(xQueueReceive( h_QueueAudioRecStatus, &_rec_status, ( TickType_t ) 100 ) != pdTRUE) { 
      _rec_status.status = REC_STATUS_NONE;
   }
   return &_rec_status;   
}


/********************************************************************
 *  @brief Start a recording. The recording variables are passed to the 
 *  background task as a pointer to a 'rec_command_t' struct. 
 *  @param duration_secs - number of seconds to record audio
 *  @param output - pointer to callers buffer. If NULL, no mem output.
 *  @param filepath - pointer to path/filename to write data to sd card.
 */
void AUDIO::startRecording(float duration_secs, int16_t *output, uint16_t samples_frame, const char *filepath)
{
   _rec_cmd.mode = REC_MODE_START;             // modes - see REC_MODE_xxx below.
   _rec_cmd.duration_secs = duration_secs;     // > 0: timed recording mode, duration in milliseconds
   _rec_cmd.samples_per_frame = samples_frame; // num 16 bit samples per frame.
   _rec_cmd.data_dest = output;                // if ptr != NULL, send signed 16 bit data to mem location
   _rec_cmd.filepath = filepath;               // if path != NULL: append data to sd card file 
   _rec_cmd.use_lowpass_filter = false;        // true enables lp filter of mic data
   _rec_cmd.filter_cutoff_freq = FILTER_CUTOFF_FREQ;  // cutoff freq (3db point) of LP filter in HZ
   // send start cmd & params to the background task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
 *  @brief Stop a current recording. If no recording is executing, this 
 *  command is ignored.
 */
void AUDIO::stopRecording(void)
{
   _rec_cmd.mode = REC_MODE_START,             // modes - see REC_MODE_xxx below.

   // send start cmd & params to the background task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
*  @brief Initialize the I2S microphone - ICS43434
*       Uses I2S chnl 0
*/
bool AUDIO::initMicrophone(uint32_t sample_rate)
{
   // Remove previous mic I2S driver (if installed)
   i2s_driver_uninstall(I2S_NUM_0);     // uninstall any previous mic driver 

   i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = sample_rate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,          //I2S_CHANNEL_FMT_RIGHT_LEFT
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,  
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4, 
      .dma_buf_len = 1024, 
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = I2S_PIN_NO_CHANGE,
   };
   static i2s_pin_config_t pin_config;
   pin_config.bck_io_num = PIN_MIC_I2S_BCLK;
   pin_config.ws_io_num = PIN_MIC_I2S_WS;
   pin_config.data_out_num = I2S_PIN_NO_CHANGE;   // no connect
   pin_config.data_in_num = PIN_MIC_I2S_DOUT; 

   if(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) == ESP_OK) {
      if(i2s_set_pin(I2S_NUM_0, &pin_config) == ESP_OK) {
         return true;
      }
   }
   return false;
}


/********************************************************************
*  @brief Initialize the I2S speaker driver - MAX98357
*  Uses I2S chnl 1
*/
bool AUDIO::initSpeaker(uint32_t sample_rate)
{
    
   i2s_driver_uninstall(I2S_NUM_1);     // uninstall any previous driver 

   i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = sample_rate,         // SAMPLE_RATE     
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // mono - dma bufr size = 16 bits / sample
      .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      .dma_buf_len = I2S_DMA_BUFR_LEN,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = I2S_PIN_NO_CHANGE};

   // and don't mess around with this
   i2s_pin_config_t i2s_spkr_pins;
      i2s_spkr_pins.mck_io_num = I2S_PIN_NO_CHANGE;    // make sure this is set to -1, else IO0 will output fixed clk
      i2s_spkr_pins.bck_io_num = PIN_SPKR_I2S_BCLK;
      i2s_spkr_pins.ws_io_num = PIN_SPKR_I2S_WS;
      i2s_spkr_pins.data_out_num = PIN_SPKR_I2S_DIN;
      i2s_spkr_pins.data_in_num = I2S_PIN_NO_CHANGE;

   // start up the I2S peripheral
   if(i2s_driver_install(I2S_NUM_1, &i2s_config, 1, NULL) != ESP_OK)
      return false;

   if(i2s_set_pin(I2S_NUM_1, &i2s_spkr_pins) != ESP_OK)
      return false;

   i2s_zero_dma_buffer(I2S_NUM_1);        // clear out spkr dma bufr

   return true;
}


/********************************************************************
*  @fn Create a header for a WAV audio file
*/
void AUDIO::CreateWavHeader(byte* header, int waveDataSize)
{
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSizeMinus8 = waveDataSize + 44 - 8;
  header[4] = (byte)(fileSizeMinus8 & 0xFF);          // LSB
  header[5] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  header[6] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  header[7] = (byte)((fileSizeMinus8 >> 24) & 0xFF);  // MSB
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;  // linear PCM
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;  // linear PCM
  header[21] = 0x00;
  header[22] = 0x01;  // monoral
  header[23] = 0x00;
  header[24] = 0x80;  // sampling rate 16000
  header[25] = 0x3E;
  header[26] = 0x00;
  header[27] = 0x00;
  header[28] = 0x00;  // Byte/sec = 16000x2x1 = 32000
  header[29] = 0x7D;
  header[30] = 0x00;
  header[31] = 0x00;
  header[32] = 0x02;  // 16bit monoral
  header[33] = 0x00;
  header[34] = 0x10;  // 16bit
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(waveDataSize & 0xFF);           // LSB
  header[41] = (byte)((waveDataSize >> 8) & 0xFF);
  header[42] = (byte)((waveDataSize >> 16) & 0xFF);
  header[43] = (byte)((waveDataSize >> 24) & 0xFF);   // MSB
}


/********************************************************************
 * Play an audio tone for a given number of milliseconds.
 */
void AUDIO::playTone(float tone_freq, float sample_rate, float volume, float duration_sec)
{
   h_QueueToneTask = xQueueCreate(5, sizeof(tone_cmd_queue_t));    // create cmd queue
   play_tone_params_t play_tone_params = {tone_freq, sample_rate, volume, duration_sec};

   // Start background audio task running in core 0
   xTaskCreate(
      playToneTask,                       // Function to implement the task 
      "Play Tone Task",                   // Name of the task (optional)
      8192,                               // Stack size in words. What should it be? dunno but if it crashes it might be too small!
      &play_tone_params,                  // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_playTone);                       // Task handle. 

   vTaskDelay(10);
}


/********************************************************************
 * Play Tone task
 */
void playToneTask(void * params)
{
   // Get params passed by reference and save in local variables
   play_tone_params_t *play_params = (play_tone_params_t *)params;
   float tone_freq = play_params->tone_freq;
   float duration_sec = play_params->duration_sec;
   float sample_rate = play_params->sample_rate;
   float volume = play_params->volume;

   // Variables for building sinewave
   uint32_t i, j;
   int32_t idx;
   int16_t ismpl;
   int32_t bufr_pad;
   // float num_cycles = (1000000.0 / tone_freq) * duration_sec; 
   float samples_per_cycle = sample_rate / tone_freq; // number of 16 bit samples per cycle

   tone_cmd_queue_t tone_cmd;

   // Calc number of bytes for sinewave of the specified duration
   float total_samples;
   if(duration_sec > 0.0) {
      total_samples = (sample_rate * duration_sec);
      // Set bufr pad length
      if(int(total_samples) % I2S_DMA_BUFR_LEN != 0)  // is data evenly divisible into tone buffer?
         bufr_pad = (I2S_DMA_BUFR_LEN - (int(total_samples) % I2S_DMA_BUFR_LEN)) * sizeof(int16_t);    
      else  
         bufr_pad = 0;  
   } else {
      total_samples = I2S_DMA_BUFR_LEN;
      bufr_pad = 0;
   }
   float total_bytes = total_samples * sizeof(int16_t);     // bytes = samples * 2

   // Create a 1K sample buffer
   uint8_t *sample_buf = (uint8_t *)heap_caps_malloc(I2S_DMA_BUFR_LEN * sizeof(int16_t), MALLOC_CAP_SPIRAM); 
   uint32_t bytes_written;
   esp_err_t err;
   float ratio = (PI * 2) * tone_freq / sample_rate;  // constant for sinewave

   memset(sample_buf, 0x0, I2S_DMA_BUFR_LEN * sizeof(int16_t));   // clear sample bufr 
   idx = 0;
   j = 0;
   while(true) {
      tone_cmd.cmd = TONE_CMD_NONE;
      // Check queue for any new commands
      if(xQueueReceive(h_QueueToneTask, &tone_cmd, 0)) { }  // check for commands
      // Generate a sinewave and write to I2S device
      if(tone_cmd.cmd != TONE_CMD_CLOSE) {
         // Fill the sample bufr with repetative sinewave cycles
         for (i = 0; i < int(total_samples); i++) {
            ismpl = int16_t(volume * sin(j * ratio) / 2.0); // Build data with positive and negative values
            if(++j >= int(round(samples_per_cycle))) 
               j = 0;
            sample_buf[idx] = ismpl & 0xFF;     // LSB
            sample_buf[idx+1] = ismpl >> 8;     // MSB
            idx += 2;
            // If buffer is full, write data to i2s device
            if(idx >= I2S_DMA_BUFR_LEN * sizeof(int16_t)) {
                  // Serial.println("Writing tone");
               err = i2s_write(I2S_NUM_1, (uint8_t *)sample_buf, idx, &bytes_written, portMAX_DELAY);
               if(err != ESP_OK)
                  Serial.printf("i2s err: %d\n", err);
               idx = 0;
            } 
         }   
         // Fill padded area at the end of the bufr with 0's
         if(bufr_pad > 0) {
            for(i=0; i<bufr_pad; i++)  
               sample_buf[idx + i] = 0x0;
                  // Serial.println("writing pad");
            err = i2s_write(I2S_NUM_1, (uint8_t *)sample_buf, I2S_DMA_BUFR_LEN * sizeof(int16_t), &bytes_written, portMAX_DELAY);
         }
         // Close task if finite duration or if I2S write error
         if(duration_sec > 0.0 || err != ESP_OK) {
            break;
         }
      } else {
         break;
      }
   } 
   /**
    * Exit this task. Free memory 
    */
   vQueueDelete(h_QueueToneTask);         // free command queue memory      
   free(sample_buf);                      // free internal 1k sample bufr in PSRAM
   h_playTone = NULL;                     // task handle null   
   vTaskDelete(NULL);                     // outahere!   
}


/********************************************************************
 * Stop the tone generator
 */
void AUDIO::stopTone(void)
{
   tone_cmd_queue_t tone_cmd;
   tone_cmd.cmd = TONE_CMD_CLOSE;
   xQueueSend( h_QueueToneTask, ( void * ) &tone_cmd, ( TickType_t ) 100 ); // notify that 
}


/********************************************************************
*  @fn Play raw uncompressed audio from a buffer in memory.
*/
bool AUDIO::playRawAudio(uint8_t *audio_in, uint32_t len, uint16_t volume)
{
   esp_err_t err;
   uint32_t bytes_written = 0;
   uint32_t i;
   int16_t v;
   if(volume > 100) volume = 100;   // constrain volume to 100 max
   float vol = float(volume) / 100.0;  // volume is from 0.0 to 1.0
   float newvol;

   if(len == 0)
      return false;

   /**
   *  @brief Apply volume control to audio data.
   */
   if(volume > 0) {
      for(i=0; i<len; i+=2) {
         v = audio_in[i+1];
         v = (v << 8) | audio_in[i];
         newvol = float(v) * vol;
         v = int(newvol);
         audio_in[i+1] = v >> 8;
         audio_in[i] = v & 0XFF;
      }
   }
   err = i2s_write(I2S_NUM_1, (uint16_t *)audio_in, len, &bytes_written, portMAX_DELAY);
         // Serial.printf("bytes written=%d\n", bytes_written);
   if(err != ESP_OK || bytes_written == 0) {
      Serial.printf("[Error]: i2s_write error, bytes written=%d", bytes_written);
      return false;
   }   
   return true;
}


/********************************************************************
*  @fn Stop a currently playing WAV file.
*/
void AUDIO::stopWavPlaying(void)
{
   playWavFromSD_Stop = true;
}


/********************************************************************
*  @fn Return true if audio is currently playing.
*/
bool AUDIO::isPlaying(void) 
{
   return (playWavFromSD_Progress > -1) ? true : false;
}


int8_t AUDIO::getWavPlayProgress(void)
{
   return playWavFromSD_Progress;
}      

/********************************************************************
*  @brief Play audio clips from a WAV file stored in SD card.
* 
*  @param filename - C string name of file on SD card to play.
*  @param volume - 0 - 100%
*/
bool AUDIO::playWavFromSD(const char *filename, uint16_t volume) 
{
   float vol = float(volume) / 100;
   esp_err_t err;
   int16_t v16;
   int32_t bytesRead;
   uint32_t bytesWritten = 0;
   uint32_t bytesToRead;
   uint32_t i, idx = 0;
   uint8_t num_chnls;
   int32_t sampled_data_size;
   float newvol;
   bool play_loop = true;                 // assume all is OK
   bool file_ok = false;

   #define WAV_BUFR_SIZE      (512 * sizeof(uint16_t))   // chunk size

   playWavFromSD_Stop = false;
   playWavFromSD_Progress = 0;

   // Create temp buffer in PSRAM
   uint8_t *buffer = (uint8_t *)heap_caps_malloc(WAV_BUFR_SIZE + 16, MALLOC_CAP_SPIRAM); 
   if(!buffer) {
      return false;
   }

   /**
    * @brief Get file size to calc progress
    */
   int32_t file_sz = sdcard.getFileSize(filename);
   if(file_sz < 1) {
      free(buffer);
      return false;
   }

   /**
    * @brief Open file for multiple chunk reads if all is OK so far
    */
   file_ok = sdcard.fileOpen(filename, FILE_READ, false);
   if(file_ok) {
      bytesRead = sdcard.fileRead(buffer, 128, 0); // read 1st 128 bytes (header area)
      play_loop = (bytesRead >= 128) ? true : false;
   } else {
      free(buffer);
      return false;
   }

   // validate this file is a WAV file - signature == RIFF
   if(play_loop) {
      if(strncmp((const char *)buffer, "RIFF", 4) != 0) {
         play_loop = false;            // remove self    
      }

      num_chnls = buffer[22];          // get num channels from header

      // search for tag 'data' - start of sampled data
      for(i=32; i<128; i++) {
         if(strncmp((char *)buffer+i, "data", 4) == 0) {
            idx = i;
            break;
         }
      }
      play_loop = (idx > 0) ? true : false;              // exit if 'data' substring not found   

      // extract data size from WAV header
      memcpy(&sampled_data_size, (uint8_t *)buffer+idx+4, 4); // get data size
      idx += 8;                           // advance index to start of sampled data
      sampled_data_size -= idx;           // remove header size from total data
      if(sampled_data_size <= 0) 
         play_loop = false;
   }

   /**
   *  @brief Loop processing audio data and writing to output device (speaker)
   */
   while(play_loop) { 

      // Check queue for commands 
      if(playWavFromSD_Stop) 
         break;

      bytesToRead = (sampled_data_size < WAV_BUFR_SIZE) ? sampled_data_size : WAV_BUFR_SIZE;
      bytesRead = sdcard.fileRead(buffer, bytesToRead, idx);  
      if(bytesRead <= 0) {
         break;
      }

      sampled_data_size -= bytesRead;     // calc remaining bytes in file
      if(sampled_data_size <= 0) {        // all done?
         break;
      }

      idx += bytesRead;                   // move seek position to end of file

      // Keep a running tab on play progress
      playWavFromSD_Progress = map(idx, 0, file_sz, 0, 100); // progress from 0 - 100%

      // if file is stereo, convert to mono
      if(num_chnls > 1) {                 // stereo data?
         for(i=0; i<bytesRead; i+=2) {    // compress data using only L chnl data (mono)
            buffer[i] = buffer[(i*2)+2];
            buffer[i+1] = buffer[(i*2)+3];            
         }
         bytesRead /= 2;                  // mono data = stereo data / 2
      }

      /**
      *  @brief Apply volume control to I2S speaker data.
      */
      if(volume > 0) {
         for(i=0; i<bytesRead; i+=2) {
            v16 = buffer[i+1];
            v16 = (v16 << 8) | buffer[i];
            newvol = float(v16) * vol;
            v16 = int(newvol);
            buffer[i+1] = v16 >> 8;
            buffer[i] = v16 & 0XFF;
         }
      }

      /**
       * @brief Write data to I2S speaker power amp (MAX98357)
       */
      err = i2s_write(I2S_NUM_1, (uint16_t *)buffer, bytesRead, &bytesWritten, portMAX_DELAY);
      if(err != ESP_OK || bytesWritten == 0) {
         break;
      }   
   }

   if(file_ok)
      sdcard.fileClose();                 // close file if it was previously opened 
   free(buffer);   
   playWavFromSD_Progress = -1;           // indicate completion
   return true;                           // return true if successful
}

