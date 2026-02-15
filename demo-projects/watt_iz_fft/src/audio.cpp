/**
 * audio.cpp
 */
#include "audio.h"

AUDIO audio;

// Audio Capture task queue handle
TaskHandle_t h_taskAudioCapture = nullptr; // creat task handle for mic capture task
QueueHandle_t qAudioRecCmds;              // queue command handle
QueueHandle_t qAudioRecStatus;            // queue status handle

// Tone task queu handle
TaskHandle_t h_playTone = nullptr;        // creat a task handle for mic bg task
QueueHandle_t h_QueueToneTask;            // queue command handle

// Play WAV file task queue handle
TaskHandle_t h_taskAudioPlayWAV = nullptr;   // creat task handle for play WAV task
QueueHandle_t h_QueueAudioPlayWAVCmd;     // queue command handle
QueueHandle_t h_QueueAudioPlayWAVStat;    // queue status handle

// Audio Play background Task
TaskHandle_t h_AudioPlay = nullptr;
QueueHandle_t qAudioPlay = nullptr;                 // queue command handle


/********************************************************************
 * @brief Read and discard read dma buffer contents
 */
void AUDIO::clearReadBuffer(void)
{
#define BUFR_DEPTH      (1024 * sizeof(uint16_t) * 2)   // clear two 1K buffers
   size_t bytes_read;
   uint8_t *junk_bufr = (uint8_t *)heap_caps_malloc(BUFR_DEPTH, MALLOC_CAP_SPIRAM);  
   i2s_read(I2S_MICROPHONE, (uint8_t *)junk_bufr, BUFR_DEPTH, &bytes_read, portMAX_DELAY);  

   heap_caps_free(junk_bufr);
}


/********************************************************************
 * @brief Initialize mic & speaker. Install 'audio capture' background 
 * task to collect microphone data. 
 */
bool AUDIO::init(uint32_t sample_rate)
{
   // Initialize Microphone I2S driver for ICS43434 mems mic
   if(!initMicrophone(sample_rate))
      return false;

   // Initialize Speaker I2S driver for MAX98357 speaker driver
   if(!initSpeaker(sample_rate)) 
      return false;

   // Make a default frame buffer
   default_frame_bufr = (int16_t *)heap_caps_malloc(DEFAULT_SAMPLES_PER_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM);        

   /**
    * @brief Start background task to perform microphone data collection.
    */      
   qAudioRecCmds = xQueueCreate(5, sizeof(capture_cmd_t));  // commands to capture task
   qAudioRecStatus = xQueueCreate(1, sizeof(capture_status_t)); // status from capture task

   static capture_cmd_t rec_cmd;
   rec_cmd.data_dest = nullptr;
   rec_cmd.duration_secs = 0;
   rec_cmd.filepath = nullptr;
   rec_cmd.filter_cutoff_freq = FILTER_CUTOFF_FREQ;
   rec_cmd.qfactor = DEFAULT_LP_FILTER_Q;
   rec_cmd.mode = CAPTURE_MODE_IDLE;
   rec_cmd.samples_per_frame = DEFAULT_SAMPLES_PER_FRAME;
   rec_cmd.use_lowpass_filter = false;
   rec_cmd.enab_vad = true;

   /**
    * @brief Start audio capture background task running in core 0
    */
   xTaskCreatePinnedToCore(
      taskCaptureAudio,                   // Function to implement the task 
      "audio_capture",                    // Name of the task (optional)
      5000,                               // Stack size in words. Use printTaskHighWaterMark() to calc.
      &rec_cmd,                           // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_taskAudioCapture,                // Task handle. 
      0);                                 // this task runs in Core 0. 

   /**
    * @brief Start the audio play task running in core 1.
    */     
   qAudioPlay = xQueueCreate(3, sizeof(audio_play_t));   // queue for sending audio frames

   xTaskCreatePinnedToCore(
      taskPlayAudio,
      "audio_play",
      2048,
      nullptr,
      3,             // > LVGL task
      &h_AudioPlay,
      1);            // run this task in core 1

   return true;
}


/********************************************************************
*  @brief Audio capture background task. Runs in core 0.
*  @note 
*  1) Capture control is managed by sending a 'capture_cmd_t'
*  structure to the command queue.   
*  2) Capture begins when a CAPTURE_MODE_RECORD mode command is set in the
*  received command struct. 
*  3) Capture ends when the 'duration_secs' parameter 
*  ends, or if the command CAPTURE_MODE_STOP mode command is received.
*  4) Captured audio will be saved on the SD Card if the 'filepath' contains a
*  valid path/filename string. A WAV file header is first written to the file 
*  followed by appending frames of 16-bit audio data.
*  5) If the data destination pointer is not NULL, a frame of data (defined 
*  by the structure item 'samples_per_frame') is transferred to the external 
*  memory each time a new frame is accumulated.
*/
void taskCaptureAudio(void * params)
{
#define MIC_GAIN_FACTOR      16           // normalize mic gain
   // pointer to task params
   capture_cmd_t *rec_cmd = (capture_cmd_t *)params;

   // copy params to local struct
   capture_cmd_t primary_cmd;             // main command struct
   capture_cmd_t shadow_cmd;              // shadow command struct
   memcpy(&primary_cmd, rec_cmd, sizeof(capture_cmd_t)); // copy passed params to local struct

   // misc variables
   uint32_t i, j, k;
   int16_t sample16; 
   // uint16_t capture_stop = 0;
   // float fl;
   size_t bytes_read;  
   size_t bytes_written;
   File file_obj, file_copy_obj;
   bool file_ready = false;
   bool exec_capture = false;
   bool stop_capture = false;
   bool pause_capture = false;   
#define CAPTURE_TEMP_FILE            "/capture_temp.bin"   

   capture_status_t cap_status;
   cap_status.state = CAPTURE_STATE_NONE;   // status struct returned on request
   uint8_t wav_hdr[WAV_HEADER_SIZE + 4];  // wav file header

   /**
    * @brief Create an FFT object for VAD analysis
    */
#define CAPTURE_FFT_SIZE             512      
   ESP32S3_FFT afft;                      // FFT object
   fft_table_t *fft_table;
   fft_table = afft.init(CAPTURE_FFT_SIZE, CAPTURE_FFT_SIZE, SPECTRAL_AVERAGE); //DEFAULT_SAMPLES_PER_FRAME, SPECTRAL_AVERAGE); 

   /**
    * @brief Create a low pass filter object
    */
   ESP32S3_LP_FILTER lp_filter;

   /**
    * @brief Create a Ring (circular) Buffer for VAD
    */
#define RB_FRAME_DEPTH           6        // depth of the ring buffer in frames   
#define VAD_FRAME_DEPTH          4        // depth in frame buffer to trigger VAD

   struct {
      uint8_t *pFrames           = nullptr;  // contiguous PSRAM bufr for all frames
      int16_t head               = 0;     // next write index
      int16_t tail               = 0;     // next read index
      int16_t count              = 0;     // number of frames available
   } RingBufr ;

   uint32_t cap_frame_count      = 0;     // frame progress counter
   uint32_t max_frames           = 0;     // total frames in a finite capture
   uint32_t file_sz              = 0;
   // uint16_t pre_cap_frame_count  = 0;

   // Variables for Formant analysis of speech detection
   float E_all[3];
   float E_low[3];
   float E_high[3];
   float E_ratio[3];
   float D_all[3];      
   float noise_baseline          = 0.0f;
   bool noise_baseline_init      = false;
   uint8_t hit_count             = 0;
   bool detect_hit               = false;
   bool in_speech                = false;   
   uint8_t start_hits            = 0;
   uint8_t stop_misses           = 0;
   uint8_t missed_frames         = 0;

   // Formant band defines. A bin == 31.25 Hz
#define FFT_BIN_LOW              4        // 125 hz
#define FFT_BIN_MID              21       // 660 hz
#define FFT_BIN_HIGH             64       // 2000 hz

   const uint8_t START_HITS_REQUIRED = 2;   // attack: frames (≈200 ms)
   const uint8_t STOP_MISSES_REQUIRED = 4;  // hangover: frames (≈400 ms)   
   constexpr float EPSILON       = 1e-6f;   // small value to prevent div by zero

   uint8_t *pframe               = nullptr;
   uint16_t rb_frame_bytes       = 0;
   uint32_t rb_total             = 0;
   bool vad_detected = true;              // assume VAD not enabled
   int16_t quiet_frame_count     = 0;
   // Quiet frame interval in frames (96ms / frame)   
   uint16_t max_quiet_frames     = 20;
   uint32_t riff_size;

   // Various internal frame buffer pointers (allocated when capture cmd rcvd)
   uint8_t *mic_raw_data_bufr    = nullptr; 
   float *mic_float_bufr         = nullptr; 
   float *mic_output             = nullptr;
   float *fft_output_buf         = nullptr; 
   
   uint32_t tmo = millis();

   /**
    * @brief Infinite loop to capture frames of mic data. 
    */
   while(true) {

      /**
       * @brief Check task command queue for mode changes
       */
      if(xQueueReceive(qAudioRecCmds, &shadow_cmd, 0) == pdTRUE) {    // check for commands
         if(shadow_cmd.mode == CAPTURE_MODE_RECORD || shadow_cmd.mode == CAPTURE_MODE_INTERCOM) {
            if(!pause_capture) {
               // Copy parameters from shadow cmd struct
               memcpy(&primary_cmd, &shadow_cmd, sizeof(capture_cmd_t));  // copy all params to primary struct               
               exec_capture = true;     // Start capture
               stop_capture = false;
               cap_status.state = CAPTURE_STATE_NONE;   // no status yet
               cap_status.bufr_sel = 0;
               vad_detected = (!primary_cmd.enab_vad); // enab = !detected

               /**
                * @brief Convert capture duration to number of frames to capture.
                * @note: Duration 0.0 results in 1 frame being captured
                */
               if(primary_cmd.num_frames == 0) {   // calc num frames from duration?
                  cap_status.time_per_frame = float(primary_cmd.samples_per_frame) / float(AUDIO_SAMPLE_RATE);   // calc one frame time in ms
                  max_frames = (int(ceil(primary_cmd.duration_secs / cap_status.time_per_frame)));      
               } else {
                  max_frames = primary_cmd.num_frames;
               }
               // 
               if(shadow_cmd.mode == CAPTURE_MODE_INTERCOM)
                  max_quiet_frames = 6;
               else 
                  max_quiet_frames = 20; 

               quiet_frame_count = 0;
               cap_frame_count = 0;
               // pre_cap_frame_count = 0;

               /**
                * @brief Initialize the IIR low pass audio filter. 
                * @note: DSP functions are unique to the ESP32-S3 mcu variant!
                */
               if(primary_cmd.use_lowpass_filter) {
                  // *** Last parameter 'Qfactor' == 0.5 <smoother cutoff rate>, 1.0 <sharper cutoff rate>
                  lp_filter.init(primary_cmd.filter_cutoff_freq, float(AUDIO_SAMPLE_RATE), primary_cmd.qfactor);
               }

               /**
                * @brief If writing to a file: delete old file, open new file, & write WAV header to file
                */
               file_ready = false;                 // assume no output to file       
               if(primary_cmd.filepath && strlen(primary_cmd.filepath) > 0) {       // write audio to file?
                  sd.fremove(primary_cmd.filepath);   // delete old file if it exists
                  // Open new file for writing
                  file_obj = sd.fopen(CAPTURE_TEMP_FILE, FILE_APPEND, true);
                  file_ready = (file_obj);
               }
             
               /**
                * @brief Create new ring buffer in PSRAM
                */
               if(RingBufr.pFrames)              // if bufr previously allocated, free it
                  heap_caps_free(RingBufr.pFrames);
               rb_frame_bytes = primary_cmd.samples_per_frame * sizeof(int16_t);
               rb_total = RB_FRAME_DEPTH * rb_frame_bytes;
               RingBufr.pFrames = (uint8_t*)heap_caps_malloc(rb_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
               if (!RingBufr.pFrames) {
                  Serial.println("PSRAM allocation failed!");
               }
               // Clear ringbufr for new use
               RingBufr.head = RingBufr.tail = RingBufr.count = 0;
               noise_baseline = 0.0f;
               noise_baseline_init = false;
               hit_count = 0;
               missed_frames = 0;
               detect_hit = false;
               in_speech = false;
                              
               /**
                * @brief Create working buffers dynamically
                */
               if(mic_raw_data_bufr)
                  heap_caps_free(mic_raw_data_bufr);   // free previous buffer
               mic_raw_data_bufr = (uint8_t *)heap_caps_malloc((primary_cmd.samples_per_frame * sizeof(int32_t)) + 256, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);  

               if(mic_float_bufr)
                  heap_caps_free(mic_float_bufr);
               mic_float_bufr = (float *)heap_caps_malloc((primary_cmd.samples_per_frame * sizeof(float)) + 256, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);

               if(mic_output)
                  heap_caps_free(mic_output);
               mic_output = (float *) heap_caps_aligned_alloc(32, (primary_cmd.samples_per_frame * sizeof(float)) + 256, MALLOC_CAP_SPIRAM);                

               if(fft_output_buf)
                  heap_caps_free(fft_output_buf);
               fft_output_buf = (float *) heap_caps_aligned_alloc(32, (CAPTURE_FFT_SIZE * sizeof(float)) + 256, MALLOC_CAP_SPIRAM);           

            // *** END > CAPTURE_MODE_RECORD          
            } else {
               pause_capture = false;
            }
         } else if(shadow_cmd.mode == CAPTURE_MODE_PAUSE) {
            pause_capture = true;
         } else if(shadow_cmd.mode == CAPTURE_MODE_STOP) {  // stop capture 
            exec_capture = false;
            stop_capture = true;
         } else if(shadow_cmd.mode == CAPTURE_MODE_KILL) { // delete task?
            break;            
         }

         /**
          * @brief Update status - push status into queue
          */
         // Clear rec, pause, & frame avail bits. No other bits are affected
         cap_status.state &= ~(CAPTURE_STATE_RECORDING | CAPTURE_STATE_PAUSED | CAPTURE_STATE_FRAME_AVAIL);
         cap_status.max_frames = max_frames;
         cap_status.captured_frames = cap_frame_count;
         cap_status.max_secs = cap_status.time_per_frame * cap_status.max_frames;
         cap_status.elapsed_secs = cap_status.time_per_frame * cap_status.captured_frames;
         if(exec_capture)
            cap_status.state |= CAPTURE_STATE_RECORDING;  // report progress on demand
         if(pause_capture)
            cap_status.state |= CAPTURE_STATE_PAUSED;
         if(in_speech) 
            cap_status.state |= CAPTURE_STATE_IN_SPEECH;
         else 
            cap_status.state |= CAPTURE_STATE_IN_QUIET;
         // Send the combined (or'ed) status back via status queue
         xQueueOverwrite(qAudioRecStatus, &cap_status);  // always the latest status in the queue
      }     // *** END Queue Command Receive

      /**
       * Read audio samples from the mic I2S bus
       */
      if(exec_capture && !pause_capture) {
         // Read one frame of mic data from I2S bus
         i2s_read(I2S_MICROPHONE, (uint8_t *)mic_raw_data_bufr, primary_cmd.samples_per_frame * sizeof(int32_t), 
               &bytes_read, portMAX_DELAY);  
         uint16_t num_samples = bytes_read / sizeof(int32_t);

         /**
         * @brief Crunch 24 bit mic audio samples into signed 16 bit values 
         */     
         pframe = RingBufr.pFrames + (RingBufr.head * rb_frame_bytes); // Get next avail frame
         // <PUSH> data into new RingBufr space. If RingBufr full, overwrites oldest frame
         for (j = 0; j < num_samples; j++) {    // Mic samples are 24 bits in 32 bit value
            sample16 = mic_raw_data_bufr[(j*4)+3];                 // MSB of 24 bit data
            sample16 = sample16 << 8 | mic_raw_data_bufr[(j*4)+2];  // LSB "  "    
            sample16 *= MIC_GAIN_FACTOR;   // Normalize Mic gain for audio volume    
            // Save mic audio as int16_t data in the RingBufr
            reinterpret_cast<int16_t*>(pframe)[j] = sample16; 
            // Also, save as float data for LP filter & FFT functions
            mic_float_bufr[j] = float(sample16);   
         }

         /** 
          * @brief Update RingBufr pointer to next available frame 
          */
         if(primary_cmd.enab_vad) {
            RingBufr.head = (RingBufr.head + 1) % RB_FRAME_DEPTH;
            if (RingBufr.count == RB_FRAME_DEPTH) {   // ring bufr full?
               RingBufr.tail = (RingBufr.tail + 1) % RB_FRAME_DEPTH;  // overwrite oldest
            } else {
               RingBufr.count++;
            }
         }         

         /**
          * @brief Apply butterworth LP audio filter to mic data. This uses an optimized 
          * DSP function that is unique to the ESP32-S3 MCU.
          */
         if(primary_cmd.use_lowpass_filter) {
            lp_filter.apply(mic_float_bufr, mic_output, num_samples);

            // Convert float data back to signed 16 bit integer and save in RingBufr
            for(j=0; j<primary_cmd.samples_per_frame; j++) {
               reinterpret_cast<int16_t*>(pframe)[j] = int16_t(mic_output[j]);
            }
         }         

         /**
          * @brief If VAD (Valid Audio Detect) feature is enabled, use Formant 
          * analysis to detect speech and trigger VAD (Valid Audio Detect). The 
          * feature will also auto-end the capture after a short period of
          * non-speech (approx 2 secs).
          */ 
         if(primary_cmd.enab_vad) {
            hit_count = 0;
            for(j=0; j<3; j++) {       // iterate through 3 subframes of 512 samples
               if(primary_cmd.use_lowpass_filter) {
                  // Perform 512 point FFT on the mic float data.  
                  afft.compute(mic_output + (j*512), fft_output_buf, true);
               } else { 
                  afft.compute(mic_float_bufr + (j*512), fft_output_buf, true); 
               }
               // Clear the energy variables
               E_low[j]                   = 0.0;
               E_high[j]                  = 0.0;
               E_all[j]                   = 0.0;

               // Tuneables for VAD detector
               const float T_D_START      = 0.8f;
               const float T_D_CONT       = 0.4f;  // hysteresis value            
               const float T_BAL_START    = 1.2f;  // BAL less than 1.0 is quite, neg is HF    
               const float T_BAL_CONT     = 1.0f;  // hysteeysis value  
               
               // Apply hysteresis to energy & LF/HF ratio thresholds
               float td   = (in_speech) ? T_D_CONT   : T_D_START;
               float tbal = (in_speech) ? T_BAL_CONT : T_BAL_START;      

               // Each FFT bin == 31.25 Hz
               for(k=FFT_BIN_LOW; k<FFT_BIN_HIGH; k++) {    // avg energy bands from 125 - 1500 hz
                  float v = fft_output_buf[k];
                  E_all[j] += v; // sum of the spectrum (BIN_LOW to BIN_HIGH)
                  if(k < FFT_BIN_MID)  E_low[j] += v;
                  else                 E_high[j] += v;
               }
               // Normalize the total energy spectrum: Log of E_all
               E_all[j] = logf((E_all[j] / float(FFT_BIN_HIGH - FFT_BIN_LOW)) + EPSILON);   // avg sound energy across spectrum

               // Do once after capture starts
               if(!noise_baseline_init) {
                  noise_baseline = E_all[j]; // baseline snapshot of first frame
                  noise_baseline_init = true;
               } 
               // Running avg of noise floor
               D_all[j] = E_all[j] - noise_baseline;

               // Tuneables for noise floor averaging
               const float VAD_BASE_GATE =   0.30f;   // starting point; *tuneable*
               const float VAD_BASE_ALPHA =  0.05f;   // quiet tracking speed *tuneable*

               // Gate the 'D' value to track noise floor (self adjusting)
               if(D_all[j] < VAD_BASE_GATE && !in_speech) {
                  noise_baseline = (1.0f - VAD_BASE_ALPHA) * noise_baseline +
                        VAD_BASE_ALPHA * E_all[j];
               }
               // Calc band energy for this subframe
               E_low[j] = logf((E_low[j] / float(FFT_BIN_MID - FFT_BIN_LOW)) + EPSILON); 
               E_high[j] = logf((E_high[j] / float(FFT_BIN_HIGH - FFT_BIN_MID)) + EPSILON);    
               E_ratio[j] = E_low[j] - E_high[j]; // LOG(LF/HF)

               // Analyse rolling noise floor avg 'D' and the LF/HF ratio for possible speech
               if(D_all[j] > td && E_ratio[j] > tbal) hit_count++;
            }
            // Frame speech evidence
            bool start_hit = (hit_count >= 2);   // start: 2-out-of-3
            bool cont_hit  = (hit_count >= 1);   // continue: 1-out-of-3

            if(!in_speech) {
               // Start immediately when start criterion is met (keep your behavior)
               in_speech = start_hit;
               missed_frames = 0;
            } else {
               // In speech: require several consecutive misses to drop out (hangover)
               if(cont_hit) {
                  missed_frames = 0;
                  in_speech = true;
               } else {
                  if(++missed_frames >= STOP_MISSES_REQUIRED) {
                     in_speech = false;
                     missed_frames = 0;
                  } else {
                     in_speech = true; // stay latched during hangover
                  }
               }
            }

            // Trigger Valid Audio Detect here
            if(in_speech && !vad_detected) {
               // Don't trigger VAD until ring bufr has content
               vad_detected = (RingBufr.count >= VAD_FRAME_DEPTH); //RB_FRAME_DEPTH); 
               if(vad_detected) {
                  quiet_frame_count = 0;  // start quiet period count
                        Serial.println("VAD>>>");
               }
            }

            // VAD found, now search for quiet interval to auto-end capture
            if(primary_cmd.enab_vad && vad_detected) {   // only works if VAD feature is enabled
               if(hit_count == 0) 
                  quiet_frame_count++;    // if quiet frame incr
               else if(hit_count >= 2) 
                  quiet_frame_count--;    // if strong speech decr
               if(quiet_frame_count < 0) 
                  quiet_frame_count = 0;  // constrain to positive value
            }
         }

         // Get pointer to oldest frame in the ring buffer
         pframe = RingBufr.pFrames + (RingBufr.tail * rb_frame_bytes); 

         /** 
          * @brief If VAD detected, check for finite capture complete.
          */
         if(vad_detected && max_frames > 0 && cap_frame_count >= max_frames) {   // never stop capturing if max_frames == 0
            stop_capture = true;
         } 
         
         // Check if quiet frames will end capture
         if(primary_cmd.enab_vad && vad_detected && quiet_frame_count >= max_quiet_frames) {
            stop_capture = true;
         }

         /** 
          * @brief Write one frame of mic audio to file on the SD Card and/or to
          * external memory.
          */   
         if(!stop_capture) {                         
            if(!primary_cmd.enab_vad || primary_cmd.mode == CAPTURE_MODE_INTERCOM) { // RingBufr <PEEK> 
               if(file_ready) {              // output to file?
                  sd.fwrite(file_obj, (uint8_t *)pframe, rb_frame_bytes);
               } 
               // Send frames to external memory location?
               if(primary_cmd.data_dest) {   // validate pointer
                  memcpy((uint8_t *)primary_cmd.data_dest, (uint8_t *)pframe, rb_frame_bytes);
               }
               // Prepare status for Intercom audio capture
               cap_status.state |= CAPTURE_STATE_FRAME_AVAIL;
               if(in_speech)
                  cap_status.state |= CAPTURE_STATE_IN_SPEECH;
               
               // Alert caller that a new frame has been transferred to caller's memory
               cap_status.captured_frames = ++cap_frame_count;   
               cap_status.elapsed_secs = cap_status.time_per_frame * cap_status.captured_frames;                       
               xQueueOverwrite(qAudioRecStatus, &cap_status);
               cap_status.state &= ~CAPTURE_STATE_FRAME_AVAIL;  // reset frame avail

            } else if(vad_detected) {     // RingBufr <POP>
               uint8_t *_pframe = pframe; // frame ptr temp copy 
               while(RingBufr.count > 0) {   // empty the ring bufr (pre-roll data)
                  if(file_ready) {           // write pre-roll frames to sd card
                     sd.fwrite(file_obj, (uint8_t *)_pframe, rb_frame_bytes); 
                  }
                  // Send voice data to external buffer?
                  if(primary_cmd.data_dest) {
                     memcpy(primary_cmd.data_dest, (uint8_t *)_pframe, rb_frame_bytes);
                  }
                  // Update capture status
                  cap_status.state |= CAPTURE_STATE_FRAME_AVAIL;   // notify data ready  
                  cap_status.captured_frames = ++cap_frame_count;   
                  cap_status.elapsed_secs = cap_status.time_per_frame * cap_status.captured_frames;                             
                  xQueueOverwrite(qAudioRecStatus, &cap_status); 
                  cap_status.state &= ~CAPTURE_STATE_FRAME_AVAIL;  // clear frame avail                                        
               
                  // POP ringbufr to next oldest frame in the ring buffer
                  RingBufr.tail = (RingBufr.tail + 1) % RB_FRAME_DEPTH;
                  RingBufr.count--;                 
                  if(!stop_capture)     // if stopping, empty the ring bufr 
                     break;
                  // Get pointer to next frame in the ring buffer
                  _pframe = RingBufr.pFrames + (RingBufr.tail * rb_frame_bytes); // ptr to next newest frame     
               }
            } 
         }             
      }        // end *** if(exec_capture && !pause_capture) ***

      /**
       * @brief Stop the current capture and inform caller of completion
       */
      if(stop_capture) {
         stop_capture = false;          // do only once
         exec_capture = false;          // capture stopped
         pause_capture = false;
         primary_cmd.mode = CAPTURE_MODE_IDLE;   // redundant ?
         cap_status.max_frames = max_frames;
         cap_status.captured_frames = cap_frame_count;  
         cap_status.elapsed_secs = cap_status.time_per_frame * cap_status.captured_frames;                
         // Inform caller that recording is completed
         cap_status.state &= ~CAPTURE_STATE_RECORDING; // not capturing
         cap_status.state |= CAPTURE_STATE_COMPLETE;  // capture complete 
         xQueueOverwrite(qAudioRecStatus, &cap_status);  

         /**
          * @brief If capturing to a file, resize the file header with actual length
          */
         if(file_ready) {    
            sd.fclose(file_obj);          // close file that was opened previously

            // Add WAV header now that we have the correct data size
            riff_size = cap_frame_count * rb_frame_bytes; // 
            file_obj = sd.fopen(CAPTURE_TEMP_FILE, FILE_READ, false); // open *.bin file for reading
            file_copy_obj = sd.fopen(primary_cmd.filepath, FILE_APPEND, true); // copy to the final WAV file           
            if(file_obj && file_copy_obj) {
               // Create the WAV header with correct data size        
               memset(&wav_hdr, 0x0, WAV_HEADER_SIZE);
               audio.CreateWavHeader((uint8_t *)&wav_hdr, riff_size);
               memcpy((uint8_t *)mic_raw_data_bufr+40, &riff_size, 4);   
               riff_size += 36;                   
               memcpy((uint8_t *)mic_raw_data_bufr+4, &riff_size, 4); 
               // Write the WAV header      
               file_ready = sd.fwrite(file_copy_obj, (uint8_t *)wav_hdr, WAV_HEADER_SIZE);
               // Copy the voice data from the bin file to the WAV file 
               uint32_t frame_indx = 0;
               while(file_ready) {
                  bytes_read = sd.fread(file_obj, mic_raw_data_bufr, rb_frame_bytes, frame_indx);
                  if(bytes_read == 0)
                     break;
                  frame_indx += bytes_read;
                  if(!sd.fwrite(file_copy_obj, mic_raw_data_bufr, bytes_read))
                     break;
               }
               // close the files and delete the temporary file
               sd.fclose(file_obj);
               sd.fclose(file_copy_obj);
               sd.fremove(CAPTURE_TEMP_FILE);
            }
         }
      }
      vTaskDelay(1);
   }                 // ***end*** while(true)       

   /**
    * Kill the background task - release used buffer & queue memory
    */ 
   heap_caps_free(mic_raw_data_bufr);     // free internal buffers in PSRAM
   heap_caps_free(mic_float_bufr);        // used for LP filter and FFT
   heap_caps_free(mic_output);            // "    "
   heap_caps_free(fft_output_buf);        // "    "
   heap_caps_free(RingBufr.pFrames);      // free PSRAM ringbuffer
   vQueueDelete(qAudioRecStatus);         // free status queue memory 
   vQueueDelete(qAudioRecCmds);           // free command queue memory  
   afft.end();                            // free fft memory
   vTaskDelay(10);                        // wait a tad
   vTaskDelete(NULL);                     // remove this task
}


/********************************************************************
 * @brief Check if audio capture is running
 */
bool AUDIO::isCapturing(void)
{
   capture_status_t cap_stat;
   getCaptureStatus(&cap_stat, false);
   return ((cap_stat.state & CAPTURE_STATE_RECORDING) > 0) ? true : false;
}


/********************************************************************
 * @brief Request current state of the audio capture background task.
 * @param cap_stat - pointer to callers status struct
 * @param block - if true, wait for queue to receive status
 * @return True if valid status
 */
bool AUDIO::getCaptureStatus(capture_status_t *cap_stat, bool blocking)
{   
   uint32_t tick_wait_delay = 0;       
   // If non-blocking, return status immediately
   if(blocking) {
      tick_wait_delay = 1000;             // wait long enough for audio frames but not forever
   }
   if(xQueueReceive( qAudioRecStatus, cap_stat, ( TickType_t ) tick_wait_delay ) == pdTRUE) { 
      return true;
   }
   return false;                          // status invalid   
}


/********************************************************************
 *  @brief Start an audio capture. The capture variables are passed to the 
 *  background task as a pointer to a 'capture_cmd_t' struct. 
 *  @param duration_secs - number of seconds to capture audio
 *  @param output - pointer to callers buffer. If NULL, no mem output.
 *  @param filepath - pointer to path/filename to write data to sd card.
 */
void AUDIO::startCapture(uint16_t mode, float duration_secs, bool enab_vad, bool enab_lp_filter, 
      const char *filepath, int16_t *output, uint32_t num_frames, uint16_t samples_frame, float lp_cutoff_freq) 
{
   static capture_cmd_t _rec_cmd;
   _rec_cmd.mode = mode;                        // modes - see CAPTURE_MODE_xxx below.
   _rec_cmd.duration_secs = duration_secs;      // > 0: timed capture mode, duration in milliseconds
   _rec_cmd.num_frames = num_frames;
   _rec_cmd.samples_per_frame = samples_frame;  // num 16 bit samples per frame.   
   _rec_cmd.data_dest = output;                 // if ptr != NULL, send signed 16 bit data to mem location  
   _rec_cmd.filepath = filepath;                // if path != NULL: append data to sd card file      
   _rec_cmd.use_lowpass_filter = enab_lp_filter;        
   _rec_cmd.filter_cutoff_freq = lp_cutoff_freq;
   _rec_cmd.qfactor = DEFAULT_LP_FILTER_Q;
   _rec_cmd.enab_vad = enab_vad;                // begin capture when voice is detected      
   // send start cmd & params to the background task
   xQueueSend( qAudioRecCmds, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
 *  @brief Stop a current audio capture. If no capture is executing, this 
 *  command is ignored.
 */
void AUDIO::stopCapture(void)
{
   capture_cmd_t _rec_cmd;
   _rec_cmd.mode = CAPTURE_MODE_STOP;     // modes - see CAPTURE_MODE_xxx below.

   // send start cmd & params to the background task
   xQueueSend( qAudioRecCmds, ( void * ) &_rec_cmd, 100 ); // command start  
}


/********************************************************************
 *  @brief Pause a current capture operation. If no capture is executing, this 
 *  command is ignored.
 */
void AUDIO::pauseCapture(void)
{
   capture_cmd_t _rec_cmd;
   _rec_cmd.mode = CAPTURE_MODE_PAUSE;    // modes - see CAPTURE_MODE_xxx below.

   // send start cmd & params to the background task
   xQueueSend( qAudioRecCmds, ( void * ) &_rec_cmd, 100 ); // command start 
}


/********************************************************************
*  @brief Initialize the I2S microphone - ICS43434
*       Uses I2S chnl 0
*/
bool AUDIO::initMicrophone(uint32_t sample_rate)
{
   // Remove previous mic I2S driver (if installed)
   i2s_driver_uninstall(I2S_MICROPHONE);     // uninstall any previous mic driver 

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

   if(i2s_driver_install(I2S_MICROPHONE, &i2s_config, 0, NULL) == ESP_OK) {
      if(i2s_set_pin(I2S_MICROPHONE, &pin_config) == ESP_OK) {
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
    
   i2s_driver_uninstall(I2S_SPEAKER);     // uninstall any previous driver 

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
   if(i2s_driver_install(I2S_SPEAKER, &i2s_config, 1, NULL) != ESP_OK)
      return false;

   if(i2s_set_pin(I2S_SPEAKER, &i2s_spkr_pins) != ESP_OK)
      return false;

   i2s_zero_dma_buffer(I2S_SPEAKER);        // clear out spkr dma bufr

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
void AUDIO::playTone(float tone_freq, uint8_t ring_mode, float volume, float duration_sec, bool blocking)
{
   h_QueueToneTask = xQueueCreate(1, sizeof(tone_cmd_queue_t));    // create cmd queue
   play_tone_params_t play_tone_params = {tone_freq, ring_mode, volume, duration_sec};

   setAudioVolume(int16_t(volume));    // 0 - 100%

   // Start background audio task 
   xTaskCreate(
      taskPlayTone,                       // Function to implement the task 
      "Play Tone Task",                   // Name of the task (optional)
      2048,                               // Stack size in words. 
      &play_tone_params,                  // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_playTone);                       // Task handle. 

   vTaskDelay(10);
   uint32_t tmo = millis();     
   if(blocking) {                         // if blocking - wait for tone func to complete      
      while((millis() - tmo) < (duration_sec * 1000)) {
         if(!h_playTone)
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
   float sample_rate = AUDIO_SAMPLE_RATE; 
   float volume = play_params->volume;
   uint8_t ring_mode = play_params->ring_mode;
   uint16_t ring_count = 0;
   uint16_t ring_interval = 0;
   bool ring_on_off = true;
   audio_play_t audio_play;
#define VOL_NORM        30.0   

   // Variables for building sinewave
   uint32_t i, j;
   int32_t idx;
   int16_t ismpl;
   float samples_per_cycle = sample_rate / tone_freq; // number of 16 bit samples per cycle

   tone_cmd_queue_t tone_cmd;

   // Calc number of bytes for sinewave of the specified duration
   float total_samples;
   if(duration_sec > 0.0) {
      total_samples = (sample_rate * duration_sec);
   } else {
      total_samples = I2S_DMA_BUFR_LEN;   // minimum samples == 1 bufr
   }
   float total_bytes = total_samples * sizeof(int16_t);     // bytes = samples * 2

   // Create a 1K sample buffer
   uint8_t *sample_buf = (uint8_t *)heap_caps_malloc(I2S_DMA_BUFR_LEN * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); 
   esp_err_t err;
   float ratio = (PI * 2) * tone_freq / sample_rate;  // constant for sinewave

   memset(sample_buf, 0x0, I2S_DMA_BUFR_LEN * sizeof(int16_t));   // clear sample bufr 
   idx = 0;
   j = 0;
   ring_interval = 0;
   ring_on_off = true;
   // Generate a sinewave and write to I2S device   
   while(true) {
      // Fill the sample bufr with repetative sinewave cycles
      for (i = 0; i < int(total_samples); i++) {
         if(ring_mode == RING_MODE_STEADY || 
               (ring_mode == RING_MODE_CALLING && ring_count < 512 && ring_on_off) || 
               (ring_mode == RING_MODE_ALARM && ring_on_off)) {
            ismpl = int16_t(volume * sin(j * ratio) * VOL_NORM); // Build data with positive and negative values
            if(++j >= int(round(samples_per_cycle))) 
               j = 0;
         } else {
            ismpl = 0;
         }
         sample_buf[idx] = ismpl & 0xFF;     // LSB
         sample_buf[idx+1] = ismpl >> 8;     // MSB
         idx += 2;

         // If buffer is full, send to the BG play audio task 
         if(idx >= I2S_DMA_BUFR_LEN * sizeof(int16_t)) {
            // while (uxQueueMessagesWaiting(qAudioPlay) > 1) { 
            //    vTaskDelay(pdMS_TO_TICKS(1));   
            // }                       
            audio_play.cmd = PLAY_AUDIO;
            audio_play.chunk_bytes = I2S_DMA_BUFR_LEN * sizeof(int16_t);
            audio_play.chunk_depth = 4;
            audio_play.pChunk = (uint16_t *)sample_buf;
            audio_play.bytes_to_write = I2S_DMA_BUFR_LEN * sizeof(int16_t);
            audio_play.volume = volume;         
            xQueueSend(qAudioPlay, &audio_play, 125);   // play this chunk   
            // Wait for BG task to read the queue - msg ack                      
            while (uxQueueMessagesWaiting(qAudioPlay) > 0) { 
                     // Serial.print("x");  
               vTaskDelay(pdMS_TO_TICKS(1)); 
            }  

            idx = 0;
            // Check queue for stop cmd
            if(xQueueReceive(h_QueueToneTask, &tone_cmd, 0) == pdTRUE) { 
               if(tone_cmd.cmd == TONE_CMD_CLOSE) {
                  break;
               }
            } else 
               tone_cmd.cmd = TONE_CMD_NONE;
         } 

         ring_count++;
         // Make old style phone call ring sound
         if(ring_mode == RING_MODE_CALLING && ring_count >= 1024) {
            ring_count = 0;
            // Short interval creates a 'warbling' sound
            if(++ring_interval > RING_MODE_CALLING_INTERVAL) {
               ring_interval = 0;
               ring_on_off ^= true;    // toggle silent period
            }
         }
         else if(ring_mode == RING_MODE_ALARM && ring_count >= 1024) {
            ring_count = 0;            
            if(++ring_interval > RING_MODE_ALARM_INTERVAL) {
               ring_interval = 0;
               ring_on_off ^= true;    // toggle silent period
            }
         }
      }   
      if(tone_cmd.cmd == TONE_CMD_CLOSE) {  // if closing break from while loop
         break;
      }

      // Write odd remainder of data
      if(idx > 0) {  
         // wait for space in the queue
         // while (uxQueueMessagesWaiting(qAudioPlay) > 1) { 
         //    vTaskDelay(pdMS_TO_TICKS(1));   
         // }                       
         audio_play.cmd = PLAY_AUDIO;
         audio_play.chunk_bytes = I2S_DMA_BUFR_LEN * sizeof(int16_t);
         audio_play.chunk_depth = 4;
         audio_play.pChunk = (uint16_t *)sample_buf;
         audio_play.bytes_to_write = idx; 
         audio_play.volume = sys_utils.getVolume();       
         xQueueSend(qAudioPlay, &audio_play, 0);  
         // Wait for BG task to read the queue - msg ack    
         while (uxQueueMessagesWaiting(qAudioPlay) > 0) { 
                  Serial.print("y");             
            vTaskDelay(pdMS_TO_TICKS(1));   
         }                         
      }
      // Close task if finite duration or if I2S write error
      if(duration_sec > 0.0) 
         break;

      vTaskDelay(1);                      // pet the watchdog
   } 

   /**
    * @brief Free memory & exit this task. 
    */
   vTaskDelay(120);                       // let the BG player finish
   vQueueDelete(h_QueueToneTask);         // free command queue memory      
   heap_caps_free(sample_buf);            // free internal 1k sample bufr in PSRAM
   h_playTone = nullptr;                  // task handle null   
   vTaskDelete(NULL);                     // outahere!   
}


/********************************************************************
 * Stop the tone generator
 */
void AUDIO::stopTone(void)
{
   uint32_t tmo = millis();
   if(!h_playTone) return;
  
   tone_cmd_queue_t tone_cmd;
   tone_cmd.cmd = TONE_CMD_CLOSE;
   xQueueOverwrite( h_QueueToneTask, ( void * ) &tone_cmd); 
   // Block until playTone has finished
   while(h_playTone) {
      if((millis() - tmo) > 1000) break;
   }
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
   if(xQueueReceive(h_QueueAudioPlayWAVStat, &stat, 100) == pdTRUE)
      return stat.progress;
   return -1;        // return this if busy
}   


/********************************************************************
*  @brief Play audio clips from a WAV file stored in SD card.
* 
*  @param filename - C string name of file on SD card to play.
*  @param volume - 0 - 100%
*/
bool AUDIO::playWavFile(const char *filename, uint16_t volume, bool blocking, PlayWav_cb cb) 
{
   if(volume == 0 || (!filename))   // sanity check
      return false;

   setAudioVolume(volume);

   /**
    * @brief Start background task to play WAV files from SD Card.
    */     
   h_QueueAudioPlayWAVCmd = xQueueCreate(3, sizeof(play_wav_t));    // commands to capture task
   h_QueueAudioPlayWAVStat = xQueueCreate(1, sizeof(play_wav_status_t)); // status from capture task

   static play_wav_t play_wav_params;
   play_wav_params.cmd = PLAY_WAV_CONTINUE;  // play immediate
   play_wav_params.volume = volume;
   play_wav_params.filename = filename;
   play_wav_params.cb = cb;

   h_taskAudioPlayWAV = nullptr;
   clearReadBuffer();                     // clear noise from mic dma bufr

   // Start background audio task running in core 0
   xTaskCreatePinnedToCore( 
      taskPlayWAV,                        // Function to implement the task 
      "Play WAV File",                    // Name of the task (optional)
      4096,                               // Stack size in words. 
      &play_wav_params,                   // Task input parameter struct
      1,                                  // Priority of the task - higher number = higher priority
      &h_taskAudioPlayWAV,                // Task handle. 
      0);                                 // core to run in

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
   // audio play struct
   audio_play_t audio_play;
   // pointer to task params
   play_wav_t *play_wav = (play_wav_t *)params;
   // copy params to local struct
   play_wav_t play_wav_queue;         // shadow command struct

   play_wav_status_t play_wav_status;
   play_wav_status.progress = 0;
   int32_t bytesRead;
   // uint32_t bytesWritten = 0;
   uint32_t bytesToRead;
   uint32_t i, idx = 0;
   uint8_t num_chnls;
   int32_t total_data_bytes;
   File _file;
   // float newvol;
   bool play_loop = true; 
   bool file_ready = false;
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
      file_ready = (_file);
      if(file_ready) {
         bytesRead = sd.fread(_file, play_buffer, WAV_HEADER_SIZE, 0); // read WAV header 
         play_loop = (bytesRead > 0) ? true : false;  // is there any data to play?
      } else {
         play_loop = false;
      }

      // Validate that file is a WAV file - signature == RIFF
      if(play_loop) {
         if(strncmp((const char *)play_buffer, "RIFF", 4) != 0) {
            play_loop = false;            // remove self    
         }

         num_chnls = play_buffer[22];          // get num channels from header

         // Search for tag 'data' which points to start of audio data
         for(i=32; i<WAV_HEADER_SIZE; i++) {
            if(strncmp((char *)play_buffer+i, "data", 4) == 0) {
               idx = i;
               break;
            }
         }
         play_loop = (idx > 0);           // exit if 'data' substring not found   
         idx += 4;                        // ptr to embedded data size

         // extract data size from WAV header                         // advance index to start of sampled data         
         memcpy(&total_data_bytes, (uint8_t *)play_buffer+idx, 4); // get data size
         idx += 4;                        // point to start of audio data (44)
         if(total_data_bytes <= 0) 
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
               xQueueOverwrite( h_QueueAudioPlayWAVStat, ( void * ) &play_wav_status);  // send status 
               break;
         }
      }

      /**
       * @brief Read one frame of audio from file and send to speaker driver
       */
      if(play_loop && !pause_play) {

         bytesToRead = (total_data_bytes < WAV_BUFR_SIZE) ? total_data_bytes : WAV_BUFR_SIZE;
         bytesRead = sd.fread(_file, play_buffer, bytesToRead, idx);  
         if(bytesRead <= 0) {             // all done?
            break;
         }

         total_data_bytes -= bytesRead;   // calc remaining bytes in file
         if(total_data_bytes <= 0) {      // all done?
            break;
         }

         idx += bytesRead;                // move seek position towards end of file
         // // Keep a running tab on play progress
         // play_wav_status.progress = map(idx, 0, file_sz-48, 0, 100); // progress from 0 - 100%         

         // if file is stereo, convert to mono
         if(num_chnls > 1) {              // stereo data?
            for(i=0; i<bytesRead; i+=2) { // compress data using only L chnl data (mono)
               play_buffer[i] = play_buffer[(i*2)+2];
               play_buffer[i+1] = play_buffer[(i*2)+3];            
            }
            bytesRead /= 2;               // mono data = (stereo data / 2)
         }
         // Send audio struct to the background play task                   
         audio_play.cmd = PLAY_AUDIO;
         audio_play.chunk_bytes = WAV_BUFR_SIZE;   // max chunk size in bytes
         audio_play.chunk_depth = 4;               // # chunks to allocate
         audio_play.pChunk = (uint16_t *)play_buffer;
         audio_play.bytes_to_write = bytesRead;    // actual data bytes to play
         audio_play.volume = play_wav->volume; //sys_utils.getVolume();
         xQueueSend(qAudioPlay, &audio_play, 200); // play this chunk      
         // Wait for the BG task to read the struct 
         while (uxQueueMessagesWaiting(qAudioPlay) > 0) { 
            vTaskDelay(pdMS_TO_TICKS(1));   
         }      
         
         /**
          * @brief Report progress to caller
          */
            play_wav_status.progress = map(idx, 0, file_sz-WAV_BUFR_SIZE, 0, 100); // progress from 0 - 100%  
            xQueueOverwrite( h_QueueAudioPlayWAVStat, ( void * ) &play_wav_status);  // send status 
      }
      vTaskDelay(2);                      // keep the watchdog happy!
   }

   /**
    * @brief Play WAV complete. Close file, free memory used, and kill the task
    */
   vTaskDelay(120);                       // make sure bg player is done
   if(file_ready)
      sd.fclose(_file);                   // close file if it was previously opened 
   heap_caps_free(play_buffer);   
   vQueueDelete(h_QueueAudioPlayWAVCmd);  // free cmd queue memory 
   vQueueDelete(h_QueueAudioPlayWAVStat); // free status queue memory    
   h_taskAudioPlayWAV = nullptr;          // tell task has stopped
   vTaskDelete(NULL);                     // remove this task 
}


/********************************************************************
 * @brief Play Audio Task. This is the background task responsible for 
 * playing audio to the I2S sink (speaker) device. The task is 
 * activated by sending a 'audio_play_t' struct through the queue. 
 * The struct elements are defined here:
 * cmd - 8 bit command defines the action of the struct. 
 *    PLAY_AUDIO - Play raw audio.
 *    PLAY_CLOSE - Kill this task.
 *    PLAY_CONFIG - Config only. Audio is ignored. Fifo is cleared.
 * pChunk - Pointer to data to be played.
 * bytes_to_write - Number of actual bytes to write to the I2S device.
 * volume - Audio volume (0-100%) is appplied to the data.
 * chunk_bytes - Maximum 'bytes_to_write' per chunk.
 * chunk_depth - Number of chunks allocated to the fifo.
 */
void taskPlayAudio(void * params)
{
   ChunkRingFifo fifo;
   audio_play_t primary_params;
   audio_play_t sec_params;
   primary_params.pChunk = nullptr;     // no data yet
   primary_params.cmd = PLAY_NONE;
   uint32_t i;
   size_t bytes_written;
   uint16_t bytes_read;
   esp_err_t err;
   uint32_t chunk_depth = 24;
   uint32_t chunk_bytes = 240;
   primary_params.chunk_bytes = chunk_bytes;
   primary_params.chunk_depth = chunk_depth;
   memcpy(&sec_params, &primary_params, sizeof(audio_play_t));
   int16_t volume = 33;    // default volume level

   // Create a default fifo 
   fifo.create(chunk_depth, chunk_bytes);  // Initial fifo config 

   // Create a default one-chunk buffer in PSRAM
   int16_t *_buf = (int16_t*)heap_caps_malloc(chunk_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   memset(_buf, 0, chunk_bytes);
   bool pause_task = false;

   /**
    * @brief Infinite loop to write audio to the I2S sink device (MAX89357)
    */
   while(true) {
      // Check if incomming data/cmd
      if(xQueueReceive(qAudioPlay, &sec_params, 0) == pdTRUE) {  
         if(sec_params.cmd == PLAY_SET_VOLUME) {   // only change volume?
            volume = sec_params.volume;   // only modify primary volume 
         } else {
            memcpy(&primary_params, &sec_params, sizeof(audio_play_t)); // new primary params
            // fifo is reconfigured if chunk size & depth have changed  
            if(primary_params.chunk_bytes != chunk_bytes || 
                  primary_params.chunk_depth != chunk_depth) {   // reconfigure?
               chunk_bytes = primary_params.chunk_bytes;  // save new chunk config
               chunk_depth = primary_params.chunk_depth;
               if(_buf) {
                  heap_caps_free(_buf);      // free current PSRAM bufr
               }
               // Create new one chunk buffer in PSRAM
               _buf = (int16_t*)heap_caps_malloc(chunk_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
               memset(_buf, 0, chunk_bytes); // zero the new bufr           
               fifo.reconfigure(chunk_depth, chunk_bytes);   // reconfigure the fifo
            }
            // If fifo has space, push callers chunk into fifo. Otherwise this chunk is dropped.
            if(!fifo.isFull() && primary_params.cmd == PLAY_AUDIO && primary_params.pChunk) {  // fifo must have space and caller src is valid
               fifo.push(primary_params.pChunk, primary_params.bytes_to_write); // push chunk into the fifo
            }
         }
      } 

      // Close task?
      if(primary_params.cmd == PLAY_CLOSE) {  // if closing, exit the forever loop 
         break;
      }

      // Stop playing, clear fifo, and idle
      else if(primary_params.cmd == PLAY_CLEAR) {
         fifo.clear();
         audio.clearReadBuffer();
         primary_params.cmd == PLAY_NONE;
      }

      /**
       * @brief Pop a chunk from the fifo and write it to the I2S device
       */
      else if(!fifo.isEmpty() && primary_params.cmd == PLAY_AUDIO) {
         fifo.pop(_buf, bytes_read);      // move a chunk to our local buffer
         // Apply audio volume
         for(i=0; i<bytes_read/2; i++) {
            _buf[i] = (_buf[i] * volume) / 100;  // use integer math (faster)
         }
               // Serial.print("p");

         // Write one audio chunk to the I2S sink device
         err = i2s_write(I2S_SPEAKER, (uint16_t *)_buf, bytes_read, &bytes_written, portMAX_DELAY);
         if(err != ESP_OK || bytes_written == 0) {
            Serial.printf("Error: i2s_write err=%d\n", err);
         }   
      }
      vTaskDelay(2);
   }

   /**
    * @brief Close (destroy) task and free associated memory
    */
   if(_buf) 
      heap_caps_free(_buf);               // free chunk buffer memory
   fifo.destroy();                        // destroy the fifo
   vTaskDelete(NULL);                     // kill this task
}


/********************************************************************
 * @brief Set playback volume on the fly
 */
void AUDIO::setAudioVolume(uint8_t vol)
{
   audio_play_t audio_params;
   
   audio_params.cmd = PLAY_SET_VOLUME;
   audio_params.volume = vol; //sys_utils.getVolume();
   // Wait for space available in the queue   
   while (uxQueueMessagesWaiting(qAudioPlay) > 2) { 
      vTaskDelay(pdMS_TO_TICKS(1));   
   }    
   xQueueSend(qAudioPlay, &audio_params, 200); // play this chunk
}