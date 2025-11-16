/**
 * audio.cpp
 */
#include "audio.h"

AUDIO audio;

// Microphone task queue handle
TaskHandle_t h_taskAudioRec = NULL;       // creat task handle for mic recorder task
QueueHandle_t h_QueueAudioRecCommand;     // queue command handle
QueueHandle_t h_QueueAudioRecStatus;      // queue status handle

// Tone task queu handle
TaskHandle_t h_playTone = NULL;           // creat a task handle for mic bg task
QueueHandle_t h_QueueToneTask;            // queue command handle

// Play WAV file task queue handle
TaskHandle_t h_taskAudioPlayWAV = NULL;   // creat task handle for mic recorder task
QueueHandle_t h_QueueAudioPlayWAVCmd;     // queue command handle
QueueHandle_t h_QueueAudioPlayWAVStat;    // queue status handle

AUDIO::AUDIO(void)
{
   // empty constructor
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

   static rec_command_t rec_cmd;
   rec_cmd.data_dest = nullptr;
   rec_cmd.duration_secs = 0;
   rec_cmd.filepath = nullptr;
   rec_cmd.filter_cutoff_freq = FILTER_CUTOFF_FREQ;
   rec_cmd.mode = REC_MODE_IDLE;
   rec_cmd.samples_per_frame = DEFAULT_SAMPLES_PER_FRAME;
   rec_cmd.use_lowpass_filter = false;
   rec_cmd.enab_vad = true;

   // Start background audio task running in core 0
   xTaskCreatePinnedToCore(
      taskRecordAudio,                    // Function to implement the task 
      "Recording Task",                   // Name of the task (optional)
      16384,                              // Stack size in words. What should it be? dunno but if it crashes it might be too small!
      &rec_cmd,                           // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_taskAudioRec,                    // Task handle. 
      0);                                 // this task runs in Core 0. 

   return true;
}


/********************************************************************
*  @brief Audio recording background task. Runs in core 0.
*  @note 
*  1) Recording control is managed by sending a 'rec_command_t'
*  structure to the command queue.   
*  2) Recording begins when a REC_MODE_START mode command is set in the
*  received command struct. 
*  3) Recording ends when the 'duration_secs' parameter 
*  ends, or if the command REC_MODE_STOP mode command is received.
*  4) Recording will be saved on the SD Card if the 'filepath' contains a
*  valid path/filename string. A WAV file header is first written to the file 
*  followed by appending frames of 16-bit audio data.
*  5) If the data destination pointer is not NULL, a frame of data (defined 
*  by the structure item 'samples_per_frame') is transferred to the external 
*  memory each time a new frame is accumulated.
*/
void taskRecordAudio(void * params)
{
#define MIC_GAIN_FACTOR      20           // normalize mic gain
   // pointer to task params
   rec_command_t *rec_cmd = (rec_command_t *)params;

   // copy params to local struct
   rec_command_t primary_cmd;             // main command struct
   rec_command_t shadow_cmd;              // shadow command struct
   memcpy(&primary_cmd, rec_cmd, sizeof(rec_command_t)); // copy passed params to local struct

   // misc variables
   uint32_t i, j, k;
   int16_t asample; 
   uint16_t recording_stop = 0;
   float fl;
   size_t bytes_read;  
   size_t bytes_written;
   File file_obj;
   bool file_ok = false;
   bool exec_recording = false;
   bool stop_recording = false;
   bool pause_recording = false;   

   rec_status_t rec_status;
   rec_status.status = REC_STATUS_NONE;   // status struct returned on request

   uint32_t rec_frame_count;              // frame progress counter
   uint32_t max_frames;                   // total frames in recording
   uint8_t wav_hdr[WAV_HEADER_SIZE + 4];  // wav file header

   /**
    * @brief Create an FFT object for VAD analysis
    */
#define REC_FFT_SIZE       512      
   ESP32S3_FFT afft;                      // FFT object
   fft_table_t *fft_table;
   fft_table = afft.init(REC_FFT_SIZE, DEFAULT_SAMPLES_PER_FRAME, SPECTRAL_AVERAGE); 

   // Formant energy bands for Valid Audio Detection (VAD) detection
   float fft_energy_low;
   float fft_energy_high;
   float fft_energy_all;
   float fft_energy_ratio;

   /**
    * @brief Create a Ring (circular) Buffer for VAD
    */
#define FRAME_COUNT     5                 // depth of the ring buffer in frames   
   struct {
      uint8_t *pFrames  = nullptr;        // contiguous PSRAM bufr for all frames
      int16_t head      = 0;              // next write index
      int16_t tail      = 0;              // next read index
      int16_t count     = 0;              // number of frames available
      float avg_energy[FRAME_COUNT];
      float avg_sum     = 0.0;
   } RingBufr ;

   uint8_t *pframe               = nullptr;
   uint16_t rb_frame_size        = 0;
   uint32_t rb_total             = 0;
   bool vad_detected = true;              // assume VAD not enabled
   uint16_t quiet_frame_count    = 0;

// VAD threshold - lower this value to make VAD detection more sensitive   
#define VAD_THRESHOLD            3.5      // background noise level      
#define MAX_QUIET_FRAMES         32       // about 3 secs of quiet ends recording

   // Various internal frame buffer pointers (allocated when recording cmd rcvd)
   uint8_t *mic_raw_data_bufr    = nullptr; 
   float *mic_float_bufr         = nullptr; 
   float *mic_output             = nullptr;
   float *fft_output_buf         = nullptr; 
   
   /**
    * @brief Generate coefficients for lowpass IIR filter
    */
   #define FILTER_TAPS           6
   // Float IIR coefficients for 2500 Hz cutoff filter
   float filter_coeffs[FILTER_TAPS] = {1, 1, 1, 1, 1, 1};
   float filter_delay_line[2];            // delay line
   float cutoff_freq;

   i2s_zero_dma_buffer(I2S_NUM_0);        // not sure if this is useful?

   /**
    * @brief Infinite loop to capture frames of mic data. 
    */
   while(true) {

      /**
       * @brief Check receiving command queue for mode changes
       */
      if(xQueueReceive(h_QueueAudioRecCommand, &shadow_cmd, 0) == pdTRUE) {    // check for commands
         if(shadow_cmd.mode == REC_MODE_START) {
            if(!pause_recording) {
               // Copy parameters from shadow cmd struct
               memcpy(&primary_cmd, &shadow_cmd, sizeof(rec_command_t));  // copy all params to primary struct               
               exec_recording = true;     // Start recording
               rec_status.status = REC_STATUS_NONE;   // no status yet
               vad_detected = (primary_cmd.enab_vad ^ true); // enab = !detected

               /**
                * @brief Convert recording duration to number of frames to record.
                * @note: Duration 0.0 results in 1 frame being recorded
                */
               fl = float(primary_cmd.samples_per_frame) / float(AUDIO_SAMPLE_RATE);   // one frame time in ms
               max_frames = (int(ceil(primary_cmd.duration_secs / fl)));      
               if(max_frames == 0) max_frames = 1;
               rec_frame_count = 0;

               /**
                * @brief Generate new coefficients for the IIR audio filter. 
                * @note: DSP functions are unique to the ESP32-S3 mcu variant.
                */
               if(primary_cmd.use_lowpass_filter) {
                  cutoff_freq = primary_cmd.filter_cutoff_freq / float(AUDIO_SAMPLE_RATE);
                  // NOTE: Qfactor == 0.5 <smoother cutoff rate>, 1.0 <sharper cutoff rate>
                  dsps_biquad_gen_lpf_f32((float *)&filter_coeffs, cutoff_freq, 0.707); // nominal cutoff rate
               }

               /**
                * @brief If writing to a file: delete old file, open new file, write WAV header to file
                */
               file_ok = false;                 // assume no output to file       
               if(primary_cmd.filepath != NULL && strlen(primary_cmd.filepath) > 0) {       // write audio to file?
                  sd.fremove(primary_cmd.filepath);   // delete old file if it exists
                  // Open new file for writing
                  file_obj = sd.fopen(primary_cmd.filepath, FILE_APPEND, true);
                  file_ok = (file_obj);
                  if(file_obj) {
                     uint32_t file_sz = (max_frames * (primary_cmd.samples_per_frame * sizeof(int16_t))); // + WAV_HEADER_SIZE;
                     memset(&wav_hdr, 0x0, WAV_HEADER_SIZE + 4);
                     audio.CreateWavHeader((uint8_t *)&wav_hdr, file_sz);
                     file_ok = sd.fwrite(file_obj, (uint8_t *)wav_hdr, WAV_HEADER_SIZE + 4);
                  }
               }
             
               /**
                * @brief Create new ring buffer in PSRAM
                */
               if(RingBufr.pFrames)              // if bufr previously allocated, free it
                  free(RingBufr.pFrames);
               rb_frame_size = primary_cmd.samples_per_frame * sizeof(int16_t);
               rb_total = FRAME_COUNT * rb_frame_size;
               RingBufr.pFrames = (uint8_t*)heap_caps_malloc(rb_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
               if (!RingBufr.pFrames) {
                  Serial.println("PSRAM allocation failed!");
               }
               // Clear ringbufr for new use
               RingBufr.head = RingBufr.tail = RingBufr.count = 0;
               for(i=0; i<FRAME_COUNT; i++)     // clear averaging
                  RingBufr.avg_energy[i] = 0.0;  
               
               /**
                * @brief Create working buffers dynamically
                */
               if(mic_raw_data_bufr)
                  free(mic_raw_data_bufr);   // free previous buffer
               mic_raw_data_bufr = (uint8_t *)heap_caps_malloc((primary_cmd.samples_per_frame * sizeof(int32_t)) + 256, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);  

               if(mic_float_bufr)
                  free(mic_float_bufr);
               mic_float_bufr = (float *)heap_caps_malloc((primary_cmd.samples_per_frame * sizeof(float)) + 256, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);

               if(mic_output)
                  free(mic_output);
               mic_output = (float *) heap_caps_aligned_alloc(32, (primary_cmd.samples_per_frame * sizeof(float)) + 256, MALLOC_CAP_SPIRAM);                

               if(fft_output_buf)
                  free(fft_output_buf);
               fft_output_buf = (float *) heap_caps_aligned_alloc(32, (REC_FFT_SIZE * sizeof(float)) + 256, MALLOC_CAP_SPIRAM);           

               // *** END > REC_MODE_START          
            } else {
               pause_recording = false;
            }
         } else if(shadow_cmd.mode == REC_MODE_PAUSE) {
            pause_recording = true;
         } else if(shadow_cmd.mode == REC_MODE_STOP) {  // stop recording 
            exec_recording = false;
            stop_recording = true;
         } else if(shadow_cmd.mode == REC_MODE_SEND_STATUS) {  // send status when requested
            // Clear recording & paused bits. No other bits are affected
            rec_status.status &= ~(REC_STATUS_RECORDING | REC_STATUS_PAUSED);
            rec_status.max_frames = max_frames;
            rec_status.recorded_frames = rec_frame_count;
            if(exec_recording)
               rec_status.status |= REC_STATUS_RECORDING;  // report progress on demand
            if(pause_recording)
               rec_status.status |= REC_STATUS_PAUSED;
            if(xQueueSend( h_QueueAudioRecStatus, ( void * ) &rec_status, ( TickType_t ) 20 ) != pdTRUE) { // send status 
               // Serial.println("que full");
            }
            rec_status.status = REC_STATUS_NONE;       
         } else if(shadow_cmd.mode == REC_MODE_KILL) { // delete task?
            break;
         } 
      }     // *** END Queue Command Receive

      /**
       * Read audio samples from the mic I2S bus
       */
      if(exec_recording && !pause_recording) {
         i2s_read(I2S_NUM_0, (uint8_t *)mic_raw_data_bufr, primary_cmd.samples_per_frame * sizeof(int32_t), 
               &bytes_read, portMAX_DELAY);  
         uint16_t num_samples = bytes_read / sizeof(int32_t);

         /**
         * @brief Crunch 24 bit mic audio samples into signed 16 bit values 
         */     
         pframe = RingBufr.pFrames + (RingBufr.head * rb_frame_size); // Get next avail frame
         // <PUSH> data into new RingBufr space. If RingBufr full, overwrites oldest frame
         for (j = 0; j < num_samples; j++) {    // Mic samples are 24 bits in 32 bit value
            asample = mic_raw_data_bufr[(j*4)+3];                 // MSB of 24 bit data
            asample = asample << 8 | mic_raw_data_bufr[(j*4)+2];  // LSB "  "    
            asample *= MIC_GAIN_FACTOR;   // Normalize Mic gain for audio volume    
            // Save mic audio as int16_t data in the RingBufr
            reinterpret_cast<int16_t*>(pframe)[j] = asample; 
            // Also, save as float data for LP filter & FFT functions
            mic_float_bufr[j] = float(asample);   
         }

         /** 
          * @brief Rotate RingBufr to next available frame 
          */
         if(primary_cmd.enab_vad) {
            RingBufr.head = (RingBufr.head + 1) % FRAME_COUNT;
            if (RingBufr.count == FRAME_COUNT) {
               RingBufr.tail = (RingBufr.tail + 1) % FRAME_COUNT;  // overwrite oldest
            } else {
               RingBufr.count++;
            }
         }         

         /**
          * @brief Apply butterworth audio filter to mic data. This uses an optimized DSP 
          *    function that is unique to the ESP32-S3 MCU.
          */
         if(primary_cmd.use_lowpass_filter) {
            dsps_biquad_f32_aes3(mic_float_bufr, mic_output, num_samples, 
                  (float *)&filter_coeffs, (float *)&filter_delay_line);

            // Convert float data back to signed 16 bit integer and save in RingBufr
            for(j=0; j<primary_cmd.samples_per_frame; j++) {
               reinterpret_cast<int16_t*>(pframe)[j] = int(mic_output[j]);
            }
         }         

         /**
          * @brief If VAD (Valid Audio Detect) is enabled, perform an FFT to analyse
          * if speech has been detected. 
          */ 
         if(primary_cmd.enab_vad) { // && !vad_detected) {
            if(primary_cmd.use_lowpass_filter) {
               // Perform 1K FFT on the mic float data. Compute time ~ 3.5ms.  
               afft.compute(mic_output, fft_output_buf, true);
            } else { 
               afft.compute(mic_float_bufr, fft_output_buf, true); 
            }

            fft_energy_low = 0.0;
            fft_energy_high = 0.0;
            fft_energy_all = 0.0;
            for(k=FFT_BIN_LOW; k<FFT_BIN_HIGH; k++) {    // avg energy from 300 - 2500 hz
               fft_energy_all += fft_output_buf[k];
               if(k < FFT_BIN_MID) 
                  fft_energy_low += fft_output_buf[k];
               else 
                  fft_energy_high += fft_output_buf[k];
            }
            // Compute ratio of LF to HF
            fft_energy_ratio = fft_energy_low / fft_energy_high;
            RingBufr.avg_energy[RingBufr.head] = fft_energy_ratio;
            RingBufr.avg_sum = 0.0;
            for(i=0; i<FRAME_COUNT; i++) {
               RingBufr.avg_sum += RingBufr.avg_energy[i];
            }          
            RingBufr.avg_sum /= RingBufr.count;
                     // Serial.printf("avg=%.2f\n", RingBufr.avg_sum);  
            if(RingBufr.count > 2 && !vad_detected) {      // ringbufr needs more than 'n' frames for reasonable avg
               vad_detected = (RingBufr.avg_sum > VAD_THRESHOLD) ? true : false;
               quiet_frame_count = 0;
            }
            if(vad_detected) {
               if(fft_energy_all < 250000) {
                              // Serial.printf("fl=%.1f\n", fft_energy_all);
                  quiet_frame_count++;       // count quiet period
               } else {
                  quiet_frame_count = 0;     // reset quiet period
               }
            }
         }

         /** 
          * @brief Check if recording is complete.
          */
         if(vad_detected) {
            if(++rec_frame_count >= max_frames || quiet_frame_count > MAX_QUIET_FRAMES) {   // test for completion
               stop_recording = true;
            }
         }

         // get pointer to oldest frame in the ring buffer
         pframe = RingBufr.pFrames + (RingBufr.tail * rb_frame_size); 

         /** 
          * @brief Write one frame of mic audio to file on the SD Card
          */        
         if(file_ok) {
            if(!primary_cmd.enab_vad) {   // RingBufr <PEEK>
               sd.fwrite(file_obj, (uint8_t *)pframe, rb_frame_size);
            } else if(vad_detected) {     // RingBufr <POP>
               uint8_t *_pframe = pframe; // frame ptr temp copy 
               while(RingBufr.count > 0) {
                  sd.fwrite(file_obj, (uint8_t *)_pframe, rb_frame_size);                  
                  RingBufr.tail = (RingBufr.tail + 1) % FRAME_COUNT;
                  RingBufr.count--;
                  if(!stop_recording)     // if stopping, empty the ring bufr into the file
                     break;
                  _pframe = RingBufr.pFrames + (RingBufr.tail * rb_frame_size); // next frame
               }
            }
         }         

         /**
          * @brief Copy 16 bit mic data to callers buffer (16 bit int)
          */
         if(primary_cmd.data_dest) {
            memcpy((uint8_t *)primary_cmd.data_dest, (uint8_t *)pframe, rb_frame_size);
            rec_status.status |= REC_STATUS_FRAME_AVAIL;
         }
      }    

      /**
       * @brief Stop the current recording and inform caller of completion
       */
      if(stop_recording) {
         stop_recording = false;          // do only once
         exec_recording = false;          // recording stopped
         pause_recording = false;
         primary_cmd.mode = REC_MODE_IDLE;   // redundant ?
         rec_status.max_frames = max_frames;
         rec_status.recorded_frames = rec_frame_count;         
         // inform caller that recording is completed
         rec_status.status = REC_STATUS_REC_CMPLT; // status = recording complete
         // xQueueSend( h_QueueAudioRecStatus, ( void * ) &rec_status, ( TickType_t ) 1000 ); // notify that text to speech is complete               
         if(file_ok) {          
            sd.fclose(file_obj);           // close file that was opened previously
            // Re-open the file and change the WAV header data size value
         }
      }
      vTaskDelay(1);
   }                 // ***end*** while(true)       

   /**
    * Kill the background task - release used buffer & queue memory
    */ 
   free(mic_raw_data_bufr);               // free internal buffers in PSRAM
   free(mic_float_bufr);                  // used for LP filter and FFT
   free(mic_output);                      // "    "
   free(fft_output_buf);
   free(RingBufr.pFrames);
   vQueueDelete(h_QueueAudioRecStatus);   // free status queue memory 
   vQueueDelete(h_QueueAudioRecCommand);  // free command queue memory    
   vTaskDelay(10);                        // wait a tad
   vTaskDelete(NULL);                     // remove thy self
}


/********************************************************************
 * @brief Check if recorder is active
 */
bool AUDIO::isRecording(void)
{
   rec_status_t *rec_stat = getRecordingStatus();
   return ((rec_stat->status & REC_STATUS_RECORDING) > 0) ? true : false;
}


/********************************************************************
 * @brief Request current state of the audio recording background task.
 * @return Pointer to a rec_status_t struct. See 'audio.h'
 */
rec_status_t * AUDIO::getRecordingStatus(void)
{
   rec_command_t rec_cmds;
   rec_cmds.mode = REC_MODE_SEND_STATUS;
   static rec_status_t _rec_status;       // access avail after function closes
   uint32_t tmo;

   // Request status from current recording task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &rec_cmds, ( TickType_t ) 10 );             

   // Wait for response. If timeout, return busy status
   if(xQueueReceive( h_QueueAudioRecStatus, &_rec_status, ( TickType_t ) 200 ) != pdTRUE) { 
      _rec_status.status = REC_STATUS_BUSY;  // return BUSY if the bg task doesn't respond quickly
   }
   return &_rec_status;   
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
 *  @brief Start a recording. The recording variables are passed to the 
 *  background task as a pointer to a 'rec_command_t' struct. 
 *  @param duration_secs - number of seconds to record audio
 *  @param output - pointer to callers buffer. If NULL, no mem output.
 *  @param filepath - pointer to path/filename to write data to sd card.
 */
void AUDIO::startRecording(float duration_secs, bool enab_vad, bool enab_lp_filter, 
      const char *filepath, int16_t *output, uint16_t samples_frame, float lp_cutoff_freq) 
{
   static rec_command_t _rec_cmd;
   _rec_cmd.mode = REC_MODE_START;              // modes - see REC_MODE_xxx below.
   _rec_cmd.duration_secs = duration_secs;      // > 0: timed recording mode, duration in milliseconds
   _rec_cmd.enab_vad = enab_vad;                // begin recording when voice is detected   
   _rec_cmd.use_lowpass_filter = enab_lp_filter; 
   _rec_cmd.filepath = filepath;                // if path != NULL: append data to sd card file      
   _rec_cmd.data_dest = output;                 // if ptr != NULL, send signed 16 bit data to mem location
   _rec_cmd.samples_per_frame = samples_frame;  // num 16 bit samples per frame.
   _rec_cmd.filter_cutoff_freq = lp_cutoff_freq;
   // send start cmd & params to the background task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
 *  @brief Stop a current recording. If no recording is executing, this 
 *  command is ignored.
 */
void AUDIO::stopRecording(void)
{
   rec_command_t _rec_cmd;
   _rec_cmd.mode = REC_MODE_STOP;             // modes - see REC_MODE_xxx below.

   // send start cmd & params to the background task
   xQueueSend( h_QueueAudioRecCommand, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
 *  @brief Pause a current recording. If no recording is executing, this 
 *  command is ignored.
 */
void AUDIO::pauseRecording(void)
{
   rec_command_t _rec_cmd;
   _rec_cmd.mode = REC_MODE_PAUSE;             // modes - see REC_MODE_xxx below.

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
 * Play an audio tone for a given number of seconds.
 */
void AUDIO::playTone(float tone_freq, float sample_rate, float volume, float duration_sec, bool blocking)
{
   h_QueueToneTask = xQueueCreate(5, sizeof(tone_cmd_queue_t));    // create cmd queue
   play_tone_params_t play_tone_params = {tone_freq, sample_rate, volume, duration_sec};

   // Start background audio task running in core 0
   xTaskCreate(
      taskPlayTone,                       // Function to implement the task 
      "Play Tone Task",                   // Name of the task (optional)
      8192,                               // Stack size in words. What should it be? dunno but if it crashes it might be too small!
      &play_tone_params,                  // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_playTone);                       // Task handle. 

   vTaskDelay(10);
   uint32_t tmo = millis();     
   if(blocking) {                         // if blocking - wait for tone func to complete      
      while((millis() - tmo) < (duration_sec * 1000)) {
         if(h_playTone == NULL)
            break;   
         vTaskDelay(10);                    
      }
   }
}


/********************************************************************
 * Play Tone task
 */
void taskPlayTone(void * params)
{
   // Get params passed by reference and save in local variables
   play_tone_params_t *play_params = (play_tone_params_t *)params;
   float tone_freq = play_params->tone_freq;
   float duration_sec = play_params->duration_sec;
   float sample_rate = play_params->sample_rate;
   float volume = play_params->volume;
#define VOL_NORM        10.0   

   // Variables for building sinewave
   uint32_t i, j;
   int32_t idx;
   int16_t ismpl;
   int32_t bufr_pad;
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
            ismpl = int16_t(volume * sin(j * ratio) * VOL_NORM); // Build data with positive and negative values
            if(++j >= int(round(samples_per_cycle))) 
               j = 0;
            sample_buf[idx] = ismpl & 0xFF;     // LSB
            sample_buf[idx+1] = ismpl >> 8;     // MSB
            idx += 2;
            // If buffer is full, write data to i2s device
            if(idx >= I2S_DMA_BUFR_LEN * sizeof(int16_t)) {
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
    * @brief Free memory & exit this task. 
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
*  @brief Send a command to the PlayWAV background task.
*/
bool AUDIO::sendPlayWavCommand(uint8_t cmd)
{
   play_wav_t play_wav {
      .cmd = cmd,
      .filename = nullptr,
      .volume = 0
   };   
   if(isWavPlaying()) {    // make sure player is running
      xQueueSend( h_QueueAudioPlayWAVCmd, ( void * ) &play_wav, ( TickType_t ) 100 );  // send cmd
      return true;
   }
   return false;
}


/********************************************************************
*  @fn Return true if audio is currently playing.
*/
bool AUDIO::isWavPlaying(void) 
{
   return (h_taskAudioPlayWAV == NULL) ? false : true;
}


/********************************************************************
 * @brief Get PlayWav progress 0 - 100%
 * @return -1 if no status (progress) available.
 */
int8_t AUDIO::getWavPlayProgress(void)
{
   play_wav_status_t stat;
   // make sure player is running
   if(sendPlayWavCommand(PLAY_WAV_SEND_STATUS)) { // request progress
      if(xQueueReceive(h_QueueAudioPlayWAVStat, &stat, 100) == pdTRUE)
         return stat.progress;
   }
   return -1;
}   


/********************************************************************
*  @brief Play audio clips from a WAV file stored in SD card.
* 
*  @param filename - C string name of file on SD card to play.
*  @param volume - 0 - 100%
*/
bool AUDIO::playWavFile(const char *filename, uint16_t volume, bool blocking, playwav_cb cb) 
{
   if(volume == 0 || (!filename))   // sanity check
      return false;

   /**
    * @brief Start background task to play WAV files from SD Card.
    */     
   h_QueueAudioPlayWAVCmd = xQueueCreate(5, sizeof(play_wav_t));    // commands to recording task
   h_QueueAudioPlayWAVStat = xQueueCreate(5, sizeof(play_wav_status_t)); // status from recording task

   static play_wav_t play_wav_params;
   play_wav_params.cmd = PLAY_WAV_CONTINUE;  // play immediate
   play_wav_params.volume = volume;
   play_wav_params.filename = filename;
   play_wav_params.cb = cb;

   h_taskAudioPlayWAV = NULL;

   // Start background audio task running in core 0
   xTaskCreate( //PinnedToCore(
      taskPlayWAV,                        // Function to implement the task 
      "Play WAV File",                    // Name of the task (optional)
      16384,                              // Stack size in words. What should it be? dunno but if it crashes it might be too small!
      &play_wav_params,                   // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_taskAudioPlayWAV);                // Task handle. 
      // 0);                                 // this task runs in Core 0. 

   vTaskDelay(10);

   // if blocking is true, wait for play to complete
   if(blocking) {
      while(h_taskAudioPlayWAV != NULL) {
         vTaskDelay(10);
      }
   }
      
   return true;
}


/********************************************************************
 * @brief Play WAV file in background
 */
void taskPlayWAV(void *params)
{
   // pointer to task params
   play_wav_t *play_wav = (play_wav_t *)params;
   // copy params to local struct
   play_wav_t play_wav_queue;         // shadow command struct

   play_wav_status_t play_wav_status;
   play_wav_status.progress = 0;

   float vol = float(play_wav->volume) / 100;
   // esp_err_t err;
   // int16_t v16;
   int32_t bytesRead;
   // uint32_t bytesWritten = 0;
   uint32_t bytesToRead;
   uint32_t i, idx = 0;
   uint8_t num_chnls;
   int32_t sampled_data_size;
   File _file;
   // float newvol;
   bool play_loop = true; 
   bool file_ok = false;
   bool pause_play = (play_wav->cmd == PLAY_WAV_PAUSE);  // start in pause mode?

   #define WAV_BUFR_SIZE      (DEFAULT_SAMPLES_PER_FRAME * sizeof(uint16_t))   // frame size in bytes

   // Create temp buffer in PSRAM
   uint8_t *play_buffer = (uint8_t *)heap_caps_malloc(WAV_BUFR_SIZE + 16, MALLOC_CAP_SPIRAM); 
   if(!play_buffer) {                     // out of memory - exit
      h_taskAudioPlayWAV = nullptr;
      vTaskDelay(10);
      vTaskDelete(NULL);  
   }

   /**
    * @brief Get WAV file size to calc progress
    */
   int32_t file_sz = sd.fsize(play_wav->filename);  // get size & validate file
   if(play_buffer && file_sz > 0) {    // if file exists, continue
      /**
       * @brief Open file for multiple chunk reads if all is OK so far
       */
      _file = sd.fopen(play_wav->filename, FILE_READ, false);
      file_ok = (_file);
      if(file_ok) {
         bytesRead = sd.fread(_file, play_buffer, 128, 0); // read 1st 128 bytes (header area)
         play_loop = (bytesRead >= 128) ? true : false;  // is there any data to play?
      } else {
         play_loop = false;
      }

      // validate this file is a WAV file - signature == RIFF
      if(play_loop) {
         if(strncmp((const char *)play_buffer, "RIFF", 4) != 0) {
            play_loop = false;            // remove self    
         }

         num_chnls = play_buffer[22];          // get num channels from header

         // Search for tag 'data' which points to start of audio data
         for(i=32; i<128; i++) {
            if(strncmp((char *)play_buffer+i, "data", 4) == 0) {
               idx = i;
               break;
            }
         }
         play_loop = (idx > 0) ? true : false;              // exit if 'data' substring not found   

         // extract data size from WAV header
         memcpy(&sampled_data_size, (uint8_t *)play_buffer+idx+4, 4); // get data size
         idx += 8;                           // advance index to start of sampled data
         sampled_data_size -= idx;           // remove header size from total data
         if(sampled_data_size <= 0) 
            play_loop = false;
      }
   } else 
      play_loop = false;

   /**
   *  @brief Loop processing audio data and writing to output device (speaker)
   */
   while(play_loop) { 
      /**
       * @brief Check command queue for play changes
       */
      if(xQueueReceive(h_QueueAudioPlayWAVCmd, &play_wav_queue, 0) == pdTRUE) { 
         switch(play_wav_queue.cmd) {
            case PLAY_WAV_STOP:
               play_loop = false;
               break;

            case PLAY_WAV_PAUSE:
               pause_play = true;
               break;

            case PLAY_WAV_CONTINUE:
               pause_play = false;
               play_loop = true;
               break;               

            case PLAY_WAV_SEND_STATUS:    // send progress
               play_wav_status.progress = map(idx, 0, file_sz-WAV_BUFR_SIZE, 0, 100); // progress from 0 - 100%  
               xQueueSend( h_QueueAudioPlayWAVStat, ( void * ) &play_wav_status, ( TickType_t ) 100 );  // send status 
               break;
         }
      }

      /**
       * @brief Read one frame of audio from file and send to speaker driver
       */
      if(play_loop && !pause_play) {

         bytesToRead = (sampled_data_size < WAV_BUFR_SIZE) ? sampled_data_size : WAV_BUFR_SIZE;
         bytesRead = sd.fread(_file, play_buffer, bytesToRead, idx);  
         if(bytesRead <= 0) {
            break;
         }

         sampled_data_size -= bytesRead;     // calc remaining bytes in file
         if(sampled_data_size <= 0) {        // all done?
            break;
         }

         idx += bytesRead;                   // move seek position towards end of file
         // // Keep a running tab on play progress
         // play_wav_status.progress = map(idx, 0, file_sz-48, 0, 100); // progress from 0 - 100%         

         // if file is stereo, convert to mono
         if(num_chnls > 1) {                 // stereo data?
            for(i=0; i<bytesRead; i+=2) {    // compress data using only L chnl data (mono)
               play_buffer[i] = play_buffer[(i*2)+2];
               play_buffer[i+1] = play_buffer[(i*2)+3];            
            }
            bytesRead /= 2;               // mono data = (stereo data / 2)
         }

         if(!playRawAudio(play_buffer, bytesRead, play_wav->volume)) {
            break;                        // on error, exit play_loop
         }
      }
      vTaskDelay(5);                      // keep the watchdog happy!
   }

   /**
    * @brief Play WAV complete. Close file, free memory used, and kill the task
    */
#define REPLAY_UPDATE_PLAY    1   
   if(play_wav->cb) {                     // inform caller play is complete
      play_wav->cb(REPLAY_UPDATE_PLAY);
   }
   if(file_ok)
      sd.fclose(_file);                   // close file if it was previously opened 
   free(play_buffer);   
   vQueueDelete(h_QueueAudioPlayWAVCmd);  // free cmd queue memory 
   vQueueDelete(h_QueueAudioPlayWAVStat); // free status queue memory    
   h_taskAudioPlayWAV = NULL;             // tell task has stopped
   vTaskDelete(NULL);                     // remove this task 
}


/********************************************************************
 * @brief Class wrapper for playRawAudio()
 */
bool AUDIO::playAudioMem(uint8_t *src_mem, uint32_t len, uint16_t volume)
{
   return playRawAudio(src_mem, len, volume);
}


/********************************************************************
*  @fn Play a block of uncompressed 16-bit audio from memory.
*/
bool playRawAudio(uint8_t *audio_bufr, uint32_t len, uint16_t volume)
{
   esp_err_t err;
   uint32_t bytes_written = 0;
   uint32_t i;
   int32_t v;
   if(volume > 100) volume = 100;   // constrain volume to 100 max

   if(len == 0 || audio_bufr == NULL)
      return false;

   /**
   *  @brief Apply volume factor to audio data.
   */
   int16_t *pv = reinterpret_cast<int16_t*>(audio_bufr); // need 16 bit ptr
   if(volume > 0) { 
      for(i=0; i<len/2; i++) {
         v = pv[i] * volume;
         v /= 100;                        // apply volume
         pv[i] = v;
      }
   } else {
      return true;                        // volume == 0 (no sound)
   }

   // Write data to I2S device
   err = i2s_write(I2S_NUM_1, (uint16_t *)audio_bufr, len, &bytes_written, portMAX_DELAY);
   if(err != ESP_OK || bytes_written == 0) {
      Serial.printf("Error: i2s_write err=%d\n", err);
      return false;
   }   
   return true;
}

