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
#include "sdcard.h"
#include "utils.h"

// misc defines
#define AUDIO_SAMPLE_RATE                 16000
#define FILTER_CUTOFF_FREQ                3300.0
#define MAX_SAMPLES_PER_FRAME             2048  // maximum mic 'chunk' samples per frame
#define WAV_HEADER_SIZE                   44

#define I2S_DMA_BUFR_LEN                  1024

// non-class function prototypes
void taskRecordAudio( void * params );
void playToneTask(void * params);
// void taskPlayWav(void *params);

// Structure passed to 'taskRecordAudio' to perform audio recordings
typedef struct {
   uint16_t mode;                         // modes - see REC_MODE_xxx below.
   float duration_secs;                   // duration in seconds
   uint16_t samples_per_frame;            // number of 16 bit samples per frame
   int16_t *data_dest;                    // if ptr != NULL, send signed 16 bit data to mem location
   const char *filepath;                  // if path != NULL: append data to sd card file 
   bool use_lowpass_filter;               // true enables lp filter of mic data
   float filter_cutoff_freq;              // cutoff freq (3db point) of LP filter in HZ
} rec_command_t ;

typedef struct {
   uint16_t status;
   uint32_t recorded_frames;
   uint32_t max_frames;
} rec_status_t ;

typedef struct {
   uint8_t cmd;
   char *wav_file;
   uint16_t volume;
} play_wav_t ;

typedef struct {
   float tone_freq;
   float sample_rate;
   float volume;
   float duration_sec;
} play_tone_params_t ;

typedef struct {
   uint8_t cmd;
} tone_cmd_queue_t;

// mic recording commands
enum {
   REC_MODE_IDLE=0x0,   
   REC_MODE_START,
   REC_MODE_STOP,
   REC_MODE_SEND_STATUS,
   REC_MODE_KILL,
};

// mic recorder status 
enum {
   REC_STATUS_NONE=0,
   REC_STATUS_PROGRESS,
   REC_STATUS_REC_CMPLT,
};

// Tone commands
enum {
   TONE_CMD_NONE=0,
   TONE_CMD_CLOSE,
};

// Wav player commands
enum {
   WAV_CMD_NONE=0,
   WAV_CMD_STOP,
   
};

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
      void playTone(float tone_freq, float sample_rate, float volume, float duration_sec);
      void stopTone(void);   
      bool playRawAudio(uint8_t *audio_in, uint32_t len, uint16_t volume); 
      bool playWavFromSD(const char *filename, uint16_t volume);         
      bool isPlaying(void);
      void stopWavPlaying(void);
      int8_t getWavPlayProgress(void);

      uint32_t convDurationToFrames(float duration_secs, float samples_frame=MAX_SAMPLES_PER_FRAME, float sample_rate_hz=AUDIO_SAMPLE_RATE);
      void clearReadBuffer(void);   // clears the read dma buffer of any contents
      void startRecording(float duration_secs, int16_t *output, uint16_t samples_frame, const char *filepath);
      void stopRecording(void);
      rec_status_t * getRecordingStatus(void);

      // WAV header
      static const int headerSize = 44;
      uint8_t paddedHeader[WAV_HEADER_SIZE + 4] = {0};  // The size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.


   private:
      rec_command_t _rec_cmd;
      volatile bool playWavFromSD_Stop;
      volatile int8_t playWavFromSD_Progress;
};

extern AUDIO audio;
extern QueueHandle_t h_QueueAudioRecCommand;    // queue command handle
extern QueueHandle_t h_QueueAudioRecStatus;     // queue status handle