/**
 * @brief sd_lvgl_fs.h : A class to provide a complete SD File System. 
 * The file system class accesses all low level function through the SD_MMC library
 * and wraps them in a more convenient class structure.
 * Additionally, linkage to the LVGL file system (uses a drive letter ("S:") prefix 
 * on path/filename) allows use of the extensive 3rd party libraries within the
 * LVGL environment. 
 * 
 * Key Features:
 * - Integration with LVGL 3rd party file system.
 * - Format SD with FAT32.
 * - Recursive directory list function.
 * - SD speed test useful for benchmarking SD cards.
 * - Read JSON files and extract key/value strings.
 * - All SD_MMC functions wrapped in class functions.
 * @note: ### Do not enable any of the LV_USE_FS options in lv_conf.h
 * 
 */
#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
// #include "ff.h"
#include "FS.h"
#include "lvgl.h"
#include "config.h"
#include <ArduinoJson.h>

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

#define FORMAT_MOUNT_POINT "/"
#define TMP_MKFS_PATH      "/mkfs0"       // temporary VFS path while formatting
#define SDMMC_BUS_WIDTH    4

/**
 * @brief Structure for SD Card info 
 */
typedef struct {
   uint8_t error_code;
   uint8_t card_type;
   uint64_t card_size;
   uint64_t bytes_avail;
   uint64_t bytes_used;
} sd_card_info_t ;

// SD Card error codes
enum {
   SD_UNINITIALIZED=0,
   SD_OK,
   MOUNT_FAILED,
   NOT_INSERTED,
   CARD_FULL,
};

// Speed test result struct
typedef struct {
   float wr_mbs;
   float rd_mbs;
} sd_speed_t ;

// struct for using PSRAM in recursive listing
typedef struct {
  char*  p = nullptr;      // PSRAM buffer
  size_t cap = 0;          // capacity (no '\0' included)
  size_t len = 0;          // current length
} PsBuf ;

// === LVGL integrated functions - only the basics!
// These functions are callbacks for the lv_fs_* mapped functions.
void* fs_open_cb(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode);
lv_fs_res_t fs_close_cb(lv_fs_drv_t*, void* file_p);
lv_fs_res_t fs_read_cb(lv_fs_drv_t*, void* file_p, void* buf, uint32_t btr, uint32_t* br);
lv_fs_res_t fs_write_cb(lv_fs_drv_t*, void* file_p, const void* buf, uint32_t btw, uint32_t* bw);
lv_fs_res_t fs_seek_cb(lv_fs_drv_t*, void* file_p, uint32_t pos, lv_fs_whence_t whence);
lv_fs_res_t fs_tell_cb(lv_fs_drv_t*, void* file_p, uint32_t* pos);
void * fs_dir_open_cb(lv_fs_drv_t*, const char* path);
lv_fs_res_t fs_dir_read_cb(lv_fs_drv_t*, void* rddir_p, char* fn, uint32_t fn_len);
lv_fs_res_t fs_dir_close_cb(lv_fs_drv_t*, void* rddir_p);

// Helper function to normalize path/filename strings for use with SD_MMC functions.
const char * normalize_path(const char *path);

/**
 * @brief SD_FILE_SYS Class
 */
class SD_FILE_SYS {
   public:
      SD_FILE_SYS(void);                  // empty constructor

      bool init(void);                    // init file system
      void deInit(void);                  // unmount drive
      bool isInserted(void);

      // High level file ops
      bool writeFile(const char *path, uint8_t *data, uint32_t len); 
      uint32_t readFile(const char *path, uint32_t len, uint32_t offset, uint8_t *ptr);
      bool appendFile(const char *path, uint8_t *data, uint32_t len); 

      // Low level file ops
      File fopen(const char* path, const char *mode, bool create_new);
      bool fwrite(File &_file, uint8_t *data, uint32_t len);      
      int32_t fread(File &_file, uint8_t *data, uint32_t len, uint32_t offset);
      bool fremove(const char *filename); // remove (delete) file    
      bool frename(const char *pathFrom, const char *pathTo);  // rename a file    
      bool fmkdir(const char *dirname);   // create new directory   
      bool frmdir(const char *dirname);       // remove (delete) directory
      bool fexists(const char *filename);     // check if file exists
      int32_t fsize(const char *filename);   // write file size to var 'size'
      void fclose(File &_file);               

      // Specialty functions
      const char * normalizePath(const char *path);   // wrapper for 'normalize_path' 
      bool listDirectory(const char *path, PsBuf &out, int level, int maxDepth);
      bool formatDrive(void);             // format to FAT32      
      sd_speed_t * speedTest(uint8_t test_mode); 
      char *readJsonFile(const char *filename, const char *category, const char *item);

   private:
      bool sdmmc_init_card(void);
      esp_err_t force_mkfs_fat(uint32_t au_bytes);   

};

extern sd_card_info_t sd_card_info;                // global card info struct
extern SD_FILE_SYS sd;