/**
 * audio.h
 */
#pragma once

#include <Arduino.h>
#include "driver/i2s.h"
#include "config.h"
#include "esp_dsp.h"
#include "dsps_fir.h"
#include "dsps_biquad.h"
#include "sd_lvgl_fs.h"
#include "esp32s3_fft.h"

// misc defines
#define AUDIO_SAMPLE_RATE                 16000
#define FILTER_CUTOFF_FREQ                3300.0
#define DEFAULT_SAMPLES_PER_FRAME         1536
#define WAV_HEADER_SIZE                   44

#define I2S_DMA_BUFR_LEN                  1024

// FFT 
#define FFT_SIZE                          1024  //512

// Formant bands
#define FFT_BIN_LOW                       4     // approx 125 hz
#define FFT_BIN_MID                       21    // approx 640 hz
#define FFT_BIN_HIGH                      48   // approx 1500 hz

// mic recording commands
enum {
   REC_MODE_IDLE=0x0,   
   REC_MODE_START,
   REC_MODE_STOP,
   REC_MODE_PAUSE,
   REC_MODE_SEND_STATUS,
   REC_MODE_KILL,
};

// mic recorder status flags (can be or'ed together)
enum {
   REC_STATUS_NONE=0,
   REC_STATUS_BUSY=0x0001,
   REC_STATUS_RECORDING=0x0002,           // reporting progress 0-100%
   REC_STATUS_PAUSED=0x0004,
   REC_STATUS_FRAME_AVAIL=0x0008,         // new recorded frame is available
   REC_STATUS_REC_CMPLT=0x0010,
};

// Tone commands
enum {
   TONE_CMD_NONE=0,
   TONE_CMD_CLOSE,
};

// Wav player commands
enum {
   PLAY_WAV_NONE=0,
   PLAY_WAV_STOP,
   PLAY_WAV_PAUSE,
   PLAY_WAV_CONTINUE,   
   PLAY_WAV_SEND_STATUS,
};



// non-class function prototypes
void taskRecordAudio( void * params );
void taskPlayTone(void * params);
void taskPlayWAV(void *params);
bool playRawAudio(uint8_t *audio_in, uint32_t len, uint16_t volume); 

// Template for the playwav callback that takes an int and returns nothing
typedef void (*playwav_cb)(uint8_t value);

// Structure passed to 'taskRecordAudio' to perform audio recordings
typedef struct {
   uint16_t mode = REC_MODE_IDLE;         // modes - see REC_MODE_xxx below.
   float duration_secs = 0;               // duration in seconds
   uint16_t samples_per_frame = DEFAULT_SAMPLES_PER_FRAME;  // number of 16 bit samples per frame
   int16_t *data_dest = NULL;             // if ptr != NULL, send signed 16 bit data to mem location
   const char *filepath = NULL;           // if path != NULL: append data to sd card file 
   bool use_lowpass_filter = false;       // true enables lp filter of mic data
   float filter_cutoff_freq = FILTER_CUTOFF_FREQ;  // cutoff freq (3db point) of LP filter in HZ
   bool enab_vad = true;                  // recording begins when voice is detected
} rec_command_t ;

typedef struct {
   uint16_t status;
   uint32_t recorded_frames;
   uint32_t max_frames;
} rec_status_t ;

typedef struct {
   uint8_t cmd;
   const char *filename;
   uint16_t volume;
   playwav_cb cb;
} play_wav_t ;

typedef struct {
   uint8_t status;
   uint8_t progress;
} play_wav_status_t ;

typedef struct {
   float tone_freq;
   float sample_rate;
   float volume;
   float duration_sec;
} play_tone_params_t ;

typedef struct {
   uint8_t cmd;
} tone_cmd_queue_t;


/**
 * AUDIO class
 */
class AUDIO {
   public:
      AUDIO(void);
      ~AUDIO(void);  
      bool init(uint32_t sample_rate);
      bool initMicrophone(uint32_t sample_rate);
      bool initSpeaker(uint32_t sample_rate);   
      void CreateWavHeader(byte* header, int waveDataSize);
      void playTone(float tone_freq, float sample_rate, float volume, float duration_sec, bool blocking);
      void stopTone(void);   
      bool playWavFile(const char *filename, uint16_t volume=20, bool blocking=false, playwav_cb cb=NULL);     
      bool playAudioMem(uint8_t *src_mem, uint32_t len, uint16_t volume);
      bool isWavPlaying(void);
      bool isRecording(void);
      bool sendPlayWavCommand(uint8_t cmd);    
      int8_t getWavPlayProgress(void);

      uint32_t convDurationToFrames(float duration_secs, float samples_frame=DEFAULT_SAMPLES_PER_FRAME, 
               float sample_rate_hz=AUDIO_SAMPLE_RATE);
      void clearReadBuffer(void);   // clears the read dma buffer of any contents
      void startRecording(float duration_secs=0.0, bool enab_vad=false, bool enab_lp_filter=false, 
               const char *filepath=NULL, int16_t *output=NULL, uint16_t samples_frame=DEFAULT_SAMPLES_PER_FRAME, 
               float lp_cutoff_freq=FILTER_CUTOFF_FREQ);
      void stopRecording(void);
      void pauseRecording(void);
      rec_status_t *getRecordingStatus(void);

      // WAV header
      static const int headerSize = 44;
      uint8_t paddedHeader[WAV_HEADER_SIZE + 4] = {0};  // The size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.


   private:

};

extern AUDIO audio;
extern QueueHandle_t h_QueueAudioRecCommand;    // queue command handle
extern QueueHandle_t h_QueueAudioRecStatus;     // queue status handle