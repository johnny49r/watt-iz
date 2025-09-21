/**
 * sdcard.h - SD MMC file system. this code is mainly a wrapper for the
 * SD_MMC ESP32 library with a few tweaks for the watt-iz hardware. One
 * such feature is a power cycle reset for the SD card which can recover 
 * from a card hang or crash.
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include "SD_MMC.h"
#include "FS.h"
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include "credentials.h"

/**
 * Formatting dependencies
 */
extern "C" {
   #include "driver/sdmmc_host.h"
   #include "sdmmc_cmd.h"
   #include "esp_vfs_fat.h"
   #include "diskio_sdmmc.h"   // ff_diskio_register_sdmmc
   #include "ff.h"             // f_mkfs, MKFS_PARM, etc.
   // ### One of these will exist depending on IDF version
#if __has_include("diskio_impl.h")
   #include "diskio_impl.h"    // ff_diskio_unregister (IDF 5.x)
#elif __has_include("diskio.h")
   #include "diskio.h"         // older headers; sometimes declare register/unregister
#endif   
}

#define MOUNT_POINT        "/"
#define TMP_MKFS_PATH      "/mkfs0"       // temporary VFS path while formatting
#define SDMMC_BUS_WIDTH    4

/**
 * @brief Structures for SD Card info and directory listing
 */
typedef struct {
   uint8_t error_code;
   uint8_t card_type;
   uint64_t card_size;
   uint64_t bytes_avail;
   uint64_t bytes_used;
} sd_card_info_t ;

struct FileEntry {
   String path;
   uint64_t size;
};

struct DirIndex {
  String path;
  std::vector<String> subdirs;
  std::vector<FileEntry> files;
};


#define SYS_CRED_FILENAME        "/wattiz_config.json"

// Speed test result struct
typedef struct {
   float wr_mbs;
   float rd_mbs;
} sd_speed_t ;

// SD Card error codes
enum {
   SD_UNINITIALIZED=0,
   SD_OK,
   MOUNT_FAILED,
   NOT_INSERTED,
   CARD_FULL,
};

enum {
   CRED_NONE=0,
   CRED_WIFI_SSID,
   CRED_WIFI_PASSWORD,
   CRED_GOOGLE_KEY,
   CRED_GOOGLE_ENDPOINT,
   CRED_GOOGLE_API_SERVER,
   CRED_OPENAI_KEY,
   CRED_OPENAI_ENDPOINT,
};

using fs::FS; 

enum {
   FILE_OPEN_READ,
   FILE_OPEN_WRITE,
   FILE_OPEN_APPEND,
};

/**
 * Class to control the SD Card with the 4-bit MMC bus.
 */
class SD_CARD_MMC {
   public:
      SD_CARD_MMC(void);
      bool init(void);
      void getCardInfo(sd_card_info_t *card_info);
      void unmount(uint32_t power_delay=10);
      bool isInserted(void);
      void listDir(const char * dirname, String &output, uint8_t levels=5);  
      bool makeDir(const char * path);  
      bool fileExists(const char * path); 
      bool deleteFile(const char * path);
      bool renameFile(const char * path1, const char * path2);
      bool appendFile(const char * path, uint8_t *data, uint32_t len);
      bool writeFile(const char * path, uint8_t *data, uint32_t len);
      bool readFile(const char * path, uint32_t len, uint32_t offset, uint8_t *ptr);   
      // lower level commands for increased performance   
      bool fileOpen(const char * path, const char *open_mode, bool create_new);
      void fileClose(void);     
      bool fileWrite(uint8_t *data, uint32_t len); 
      int32_t fileRead(uint8_t *data, uint32_t len, uint32_t offset);
      uint32_t getFileSize(const char *path);   
      char *readJSONfile(const char *filename, const char *category, const char *item);
      sd_card_info_t sd_card_info; // card info from init()      

      // Formatter
      bool formatSDCard(void);
      bool writeConfigFile(const char *filename);

      // Speed test
      sd_speed_t * speed_test(uint8_t test_mode);

   private:
      void credAllocMem(String text, uint8_t cred);
      String joinPath(const String& dir, const String& name);
      void buildDirIndex(FS& fs, const String& root, std::vector<DirIndex>& out,
                  size_t maxEntriesPerDir = 0);
      std::vector<DirIndex> index;

      char *_cred_str;

      // Formatter functions
      sdmmc_card_t* _sd_card = nullptr;
      bool sdmmc_init_card(void);
      esp_err_t force_mkfs_fat(uint32_t au_bytes);

};

extern SD_CARD_MMC sdcard;
