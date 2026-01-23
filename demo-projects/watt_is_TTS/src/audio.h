/**
 * @brief audio.h 
 * 
 * @note Ring Modes:
 *    RING_MODE_CALLING: A warbly old style ring tone with on/off interval of 500ms.
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
#include "utils.h"
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"

// misc defines
#define I2S_MICROPHONE                    I2S_NUM_0
#define I2S_SPEAKER                       I2S_NUM_1
#define AUDIO_SAMPLE_RATE                 16000
#define FILTER_CUTOFF_FREQ                3300.0
#define DEFAULT_SAMPLES_PER_FRAME         1536
#define WAV_HEADER_SIZE                   44

#define I2S_DMA_BUFR_LEN                  1024

// FFT 
#define FFT_SIZE                          1024  //512

// Audio recording commands
enum {
   REC_MODE_IDLE=0x0,   
   REC_MODE_RECORD,                       // normal record mode
   REC_MODE_RECORD_INTERCOM,              // record mode with gated (sync) frame output
   REC_MODE_AUTO_ADV,                     // auto advance output pointer  
   REC_MODE_STOP,                         // manual stop
   REC_MODE_PAUSE,                        // paused, record to continue
   REC_MODE_SEND_STATUS,                  // get status 
   REC_MODE_KILL,                         // kill the background task
};

// Audio recorder status flags (can be or'ed together)
enum {
   REC_STATE_NONE=0,
   REC_STATE_BUSY=0x0001,                // recording busy, try again later
   REC_STATE_RECORDING=0x0002,           // reporting progress 0-100%
   REC_STATE_PAUSED=0x0004,              // recording in pause mode
   REC_STATE_FRAME_AVAIL=0x0008,         // new recorded frame is available
   REC_STATE_REC_CMPLT=0x0010,           // recording has ended
   REC_STATE_IN_SPEECH=0x0020,           // in speech detected
   REC_STATE_IN_QUIET=0x0040,            // in quiet 
};

// Playtone commands
enum {
   TONE_CMD_NONE=0,
   TONE_CMD_CLOSE,
};

// Playtone ring modes
enum {
   RING_MODE_STEADY=0,
   RING_MODE_CALLING,
   RING_MODE_ALARM,
};

#define RING_MODE_CALLING_INTERVAL     14 // make calling sound like old school ring
#define RING_MODE_ALARM_INTERVAL       5  // make alarm sound 

// Wav player commands
enum {
   PLAY_WAV_NONE=0,
   PLAY_WAV_STOP,
   PLAY_WAV_PAUSE,
   PLAY_WAV_CONTINUE,   
   PLAY_WAV_SEND_STATUS,
};

// Audio Play Background Task Cmds
enum {
   PLAY_NONE=0,
   PLAY_AUDIO,
   PLAY_SET_VOLUME,
   PLAY_CLEAR,
   PLAY_CLOSE,
};

// non-class function prototypes
void taskRecordAudio( void * params );
void taskPlayAudio(void * params);
void taskPlayTone(void * params);
void taskPlayWAV(void *params);
// bool playRawAudio(uint8_t *audio_in, uint32_t len, uint16_t volume); 

// Template for the playwav callback that takes an int and returns nothing
typedef void (*playwav_cb)(uint8_t value);

// Useful defines
#define ENAB_VAD                          true
#define DISAB_VAD                         false
#define ENAB_LP_FILTER                    true
#define DISAB_LP_FILTER                   false

// Structure passed to 'taskRecordAudio' to perform audio recordings
typedef struct {
   uint16_t mode = REC_MODE_IDLE;         // modes - see REC_MODE_xxx below.
   float duration_secs = 0;               // duration in seconds
   uint32_t num_frames = 0;               // -or- frame count
   uint16_t samples_per_frame = DEFAULT_SAMPLES_PER_FRAME;  // number of 16 bit samples per frame
   int16_t *data_dest = NULL;             // if ptr != NULL, send signed 16 bit data to mem location
   const char *filepath = NULL;           // if path != NULL: append data to sd card file 
   bool use_lowpass_filter = false;       // true enables lp filter of mic data
   float filter_cutoff_freq = FILTER_CUTOFF_FREQ;  // cutoff freq (3db point) of LP filter in HZ
   bool enab_vad = true;                  // recording begins when voice is detected
} rec_command_t ;

typedef struct {
   uint16_t state;
   uint16_t bufr_sel;
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
   uint8_t ring_mode;
   float volume;
   float duration_sec;
} play_tone_params_t ;

typedef struct {
   uint8_t cmd;
} tone_cmd_queue_t;

// Audio Play structure passed in queue
typedef struct {
   uint8_t cmd;                           // configure audio play
   uint16_t *pChunk;                      // pointer to audio data   
   uint32_t bytes_to_write;               // Actual bytes to write (may be different than fifo chunk_bytes)
   uint8_t volume;                        // volume 0 - 100%
   uint32_t chunk_bytes;                  // number of bytes in a chunk (fifo element size)
   uint32_t chunk_depth;                  // num chunks in the fifo (number of elements)
} audio_play_t ;

/**
 * AUDIO class
 */
class AUDIO {
   public:
      AUDIO(void);
      ~AUDIO(void);  
      // Initialization
      bool init(uint32_t sample_rate);
      bool initMicrophone(uint32_t sample_rate);
      bool initSpeaker(uint32_t sample_rate);   

      // Tone functions
      void playTone(float tone_freq, uint8_t ring_mode, float volume, float duration_sec, bool blocking);
      void stopTone(void);  

      // WAV header
      static const int headerSize = 44;
      void CreateWavHeader(byte* header, int waveDataSize);      
      uint8_t paddedHeader[WAV_HEADER_SIZE + 4] = {0};  // The size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.
      
      // Play audio functions
      bool playWavFile(const char *filename, uint16_t volume=20, bool blocking=false, playwav_cb cb=NULL);     
      // bool playAudioMem(uint8_t *src_mem, uint32_t len, uint16_t volume);
      bool isWavPlaying(void);
      bool sendPlayWavCommand(uint8_t cmd);    
      int8_t getWavPlayProgress(void);

      // Audio play background task functions
      void setAudioVolume(uint8_t vol);

      // Voice recording functions
      void clearReadBuffer(void);   // clear read buffer of any contents
      void startRecording(uint16_t mode, float duration_secs=0.0, bool enab_vad=false, bool enab_lp_filter=false, 
               const char *filepath=nullptr, int16_t *output=nullptr, uint32_t num_frames=0, 
               uint16_t samples_frame=DEFAULT_SAMPLES_PER_FRAME, 
               float lp_cutoff_freq=FILTER_CUTOFF_FREQ);
      bool isRecording(void);               
      void stopRecording(void);
      void pauseRecording(void);
      rec_status_t *getRecordingStatus(void);

      // Audio encoders / decoders
      uint32_t encodeFLAC(uint16_t *input_data, uint16_t *encoded_data);   // return encoded length

   private:

};


// Ring buffer header struct
struct SlotHdr {
   uint16_t len;   // valid payload bytes in this slot
   uint16_t rsv;   // reserved/padding (keeps payload 32-bit aligned)
};

/**
 * @brief Ring buffer for audioplay task. 
 */
class ChunkRingFifo {
   public:
      ChunkRingFifo() = default;
      ~ChunkRingFifo() { destroy(); }

      // Allocate storage and initialize FIFO
      bool create(uint16_t nChunks,
               uint16_t chunkBytes,
               uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
      {
         destroy();  // free any existing storage

         _n = nChunks;
         _chunkBytes = chunkBytes;
         _slotBytes = (uint16_t)(sizeof(SlotHdr) + _chunkBytes);

         // uint32_t bytes = (uint32_t)_n * _slotBytes;
         _buf = (uint8_t*)heap_caps_malloc(_n * _slotBytes, caps);
         if (!_buf) {
            destroy();
            return false;
         }
         clear();
         return true;
      }

      // Hard reconfigure: free old buffer, allocate new one
      bool reconfigure(uint16_t nChunks,
                     uint16_t chunkBytes,
                     uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
      {
         return create(nChunks, chunkBytes, caps);
      }

      // Explicit destroy (optional; destructor also does this)
      void destroy() {
         if (_buf) {
            heap_caps_free(_buf);
            _buf = nullptr;
         }
         _n = 0;
         _chunkBytes = 0;
         _slotBytes = 0;
         _w = _r = _count = 0;

      }

      // Clear fifo pointers and clear fifo buffer
      void clear() {
         _w = _r = 0;
         _count = 0;
         memset(_buf, 0, _n * _slotBytes);         
      }

      bool isEmpty() const { return _count == 0; }
      bool isFull()  const { return _count == _n; }
      uint16_t count() const { return _count; }
      uint16_t capacity() const { return _n; }

      // max payload bytes (not counting header)
      uint16_t chunkBytes() const { return _chunkBytes; }

      // total bytes per slot (header + payload)
      uint16_t slotBytes() const { return _slotBytes; }

      // Push variable-length payload (<= maxPayloadBytes)
      bool push(const void* src, uint16_t lenBytes) {
         if (!_buf || isFull()) return false;
         if (lenBytes > _chunkBytes) return false;

         SlotHdr* h = hdrPtr(_w);
         h->len = lenBytes;
         h->rsv = 0;

         if (lenBytes) {
            memcpy(payloadPtr(_w), src, lenBytes);
         }

         _w = (_w + 1) % _n;
         _count++;
         return true;
      }

      // Pop one fixed-size chunk (copies chunkBytes into dst)
      bool pop(void* dst, uint16_t & outLenBytes) {
      if (!_buf || isEmpty()) return false;

      SlotHdr* h = hdrPtr(_r);
      uint16_t lenBytes = h->len;

      // Corruption guard only
      if (lenBytes > _chunkBytes) return false;

      if (lenBytes) {
         memcpy(dst, payloadPtr(_r), lenBytes);
      }

      outLenBytes = lenBytes;

      _r = (_r + 1) % _n;
      _count--;
      return true;
      }

   private:
      uint8_t *_buf = nullptr;   // contiguous fifo storage
      uint16_t _n = 0;           // number of chunks
      uint16_t _chunkBytes = 0;  // bytes per chunk
      uint16_t  _slotBytes = 0;  // sizeof(SlotHdr) + max payload      
      uint16_t _w = 0;           // write index
      uint16_t _r = 0;           // read index
      uint16_t _count = 0;       // number of chunks in FIFO

      // ===== Helper accessors (FIXED) =====
      uint8_t* slotPtr(uint16_t idx) const {
         return _buf + (uint32_t)idx * _slotBytes;
      }

      SlotHdr* hdrPtr(uint16_t idx) const {
         return reinterpret_cast<SlotHdr*>(slotPtr(idx));
      }

      uint8_t* payloadPtr(uint16_t idx) const {
         return slotPtr(idx) + sizeof(SlotHdr);
      }      
};



extern AUDIO audio;
extern QueueHandle_t qAudioRecCmds;       // queue command handle
extern QueueHandle_t qAudioRecStatus;     // queue status handle
// extern QueueHandle_t qAudioRecFrameGate;  // used to sync output frames
extern QueueHandle_t qAudioPlay; 
extern TaskHandle_t h_AudioPlay;
extern TaskHandle_t h_taskAudioRec;
extern TaskHandle_t h_playTone;
extern TaskHandle_t h_taskAudioPlayWAV;