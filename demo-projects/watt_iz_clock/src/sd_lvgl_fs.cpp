/**
 * @brief sd_lvgl_fs.cpp
 */
#include "sd_lvgl_fs.h"

sd_card_info_t sd_card_info;              // global card info from init()   
static char *config_str = nullptr;        // memory pointer for json config str
sdmmc_card_t* _sd_card = nullptr;         // driver struct

// Constants for mounting drive using drive letter 'S'
static const char* MOUNT_POINT = "/sdcard";  // SD_MMC mount point
static const char  DRIVE_LETTER = 'S';       // "S:path/file" inside LVGL

// SD_FILE_SYS class
SD_FILE_SYS::SD_FILE_SYS(void) 
{
   // empty constructor
}


/********************************************************************
 * @brief Call once after boot to initialize SD_MMC driver and
 * lvgl file system translation.
 */
bool SD_FILE_SYS::init(void)
{
   static lv_fs_drv_t drv;           // must persist after this function returns

   if(!isInserted()) {
      Serial.println(F("[SD] Card not Inserted!"));
      sd_card_info.error_code = NOT_INSERTED;
   }
   // Set GPIO pins used by this hardware for the SD card MMC bus
   SD_MMC.setPins(PIN_MMC_CLK, PIN_MMC_CMD, PIN_MMC_D0, PIN_MMC_D1, PIN_MMC_D2, PIN_MMC_D3);
   // 4-bit (fast) mode; increase alloc unit a bit to help streaming writes
   if(!SD_MMC.begin(MOUNT_POINT)) { //, /*mode1bit=*/false, /*format_if_mount_failed=*/false, /*max_files=*/8, /*alloc_unit=*/16 * 1024)) {
      Serial.println("[SD] mount failed");
      sd_card_info.error_code = MOUNT_FAILED;
      return false;
   }

   if (SD_MMC.cardType() == CARD_NONE) {
      Serial.println("[SD] no card");
      return false;
   }

   sd_card_info.card_type = SD_MMC.cardType();
   sd_card_info.card_size = SD_MMC.cardSize();
   sd_card_info.bytes_avail = SD_MMC.totalBytes();
   sd_card_info.bytes_used = SD_MMC.usedBytes();
   sd_card_info.error_code = SD_OK;

   // Create buffer for saving decode of JSON files
   config_str = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);   

   lv_fs_drv_init(&drv);

   drv.letter = 'S';
   drv.cache_size = 0;
   drv.open_cb  = fs_open_cb;
   drv.close_cb = fs_close_cb;
   drv.read_cb  = fs_read_cb;
   drv.write_cb = fs_write_cb;
   drv.seek_cb  = fs_seek_cb;
   drv.tell_cb  = fs_tell_cb;
   drv.dir_open_cb  = fs_dir_open_cb;
   drv.dir_read_cb  = fs_dir_read_cb;
   drv.dir_close_cb = fs_dir_close_cb;

   lv_fs_drv_register(&drv);

   Serial.printf("[LVGL] FS '%c:' ready, maps to %s\n", DRIVE_LETTER, MOUNT_POINT);
   return true;
}


/********************************************************************
 * @brief 'Normalize' file path string for use in low level SD_MMC 
 * functions. This function strips the "S:" drive letter and assures 
 * that the path/filename is always preceded with a '/' char.
 * The function can accept any of the following path scenarios:
 * > S:path/filename.ext
 * > S:/path/filename.ext
 * > /path/filename.ext
 * > path/filename.ext
 */
const char * normalize_path(const char *path)
{
#define TBUF_SZ   128                     // max path length
   static char buf[TBUF_SZ];              // overwritten on each call (not re-entrant)
   // prevent overflow of transient buffer
   if(strlen(path) < TBUF_SZ) {
      // 1) copy safely
      if (path) {
         strncpy(buf, path, sizeof(buf) - 1);
         buf[sizeof(buf) - 1] = '\0';
      } else {
         buf[0] = '\0';
      }

      // 2) modify: strip "S:" and ensure leading '/'
      if (buf[0] == 'S' && buf[1] == ':') { // && buf[2] == '/') {
         memmove(buf, buf + 2, strlen(buf + 2) + 1);   // drop "S:"
      }
      if (buf[0] != '/') {
         // prepend '/' if room
         size_t len = strlen(buf);
         if (len + 1 < sizeof(buf)) {
            memmove(buf + 1, buf, len + 1);
            buf[0] = '/';
         }
      }
   }
   else 
      buf[0] = '\0';
   return buf;  // as const char*
}


const char * SD_FILE_SYS::normalizePath(const char *path)
{
   return normalize_path(path);
}


/********************************************************************
 * @brief Recursively list contents of all directories. The function
 *    will create an output string with lines for Directory names
 *    and Filenames. 
 * @param path - beginning path, usually the root "/".
 * @param PsBuf - string output of recursive listing
 * @param depth - number of recursive levels. 0 = all levels.
 * @param maxDepth - limits depth of recursion
 * @return - true if successful
 */
bool SD_FILE_SYS::listDirectory(const char *path, PsBuf &out, int level, int maxDepth)
// bool fs_list_dir_recursive(const char *path, PsBuf &out, int level, int maxDepth) 
{
   if (level > maxDepth) 
      return false;
   
   uint16_t slen;
   char fsz[10];                 // bufr for itoa convertion

   // make temp buffer in PSRAM, big enough for any path/filename string
   char *childpath = (char*) heap_caps_malloc(256, MALLOC_CAP_SPIRAM); 
   // insure path starts with '/'
   const char *spath = normalize_path(path);

   File root = SD_MMC.open(spath);
   if (!root) {
      Serial.printf("Failed to open: %s\n", spath);
      free(childpath);
      return false;
   }

   if (!root.isDirectory()) {
      // It's a file - append directly to the output string
      strcat(out.p, "F\t\0");
      strcat(out.p, spath);
      strcat(out.p, "\t\0");

      // Append file size to the string
      itoa(root.size(), (char *)&fsz, 10);   // convert file size to ascii
      strncat(out.p, fsz, sizeof(fsz));
      strcat(out.p, "\n\0");
      root.close();
      free(childpath);      
      return true;
   }

   // It's a directory
   strcat(out.p, "D\t\0");
   strcat(out.p, spath);
   strcat(out.p, "\n\0");

   File file = root.openNextFile();
   while (file) {
      strcpy(childpath, spath);

      slen = strlen(childpath);           // find len of file
      if(slen > 0) {
         if(childpath[slen-1] != '/') {   // if no trailing slash, add one
            childpath[slen] = '/';
            childpath[slen+1] = '\0';     // null term
         }
      }
      strcat(childpath, file.name());     // add path & filename
      if (file.isDirectory()) {
         file.close(); // close handle before recursing
         // fs_list_dir_recursive(childpath, out, level + 1, maxDepth);
         listDirectory(childpath, out, level + 1, maxDepth);         
      } else {
         strcat(out.p, "F\t\0");
         strcat(out.p, childpath);
         strcat(out.p, " <\0");
         itoa(file.size(), (char *)&fsz, 10);   // convert file size to ascii
         strncat(out.p, fsz, sizeof(fsz));
         strcat(out.p, "> \n\0");
         file.close();
      }
      file = root.openNextFile();
   }
   root.close();
   free(childpath);
   return true;
}



/********************************************************************
 * @brief Return true if the SD card is inserted.
 */
bool SD_FILE_SYS::isInserted(void)
{
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);   // input w/50K pullup
   vTaskDelay(2);                         // wait for slow rise 
   return (digitalRead(PIN_SD_CARD_DETECT) == LOW);  // true if inserted
}


// ============================= Handles ============================
struct FsFileWrap { File f; };
struct FsDirWrap  { File dir; File next; };


// =========================== Callbacks ============================
/********************************************************************
 * @brief fs_open_cb - LVGL file system callback
 */
void* fs_open_cb(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) 
{
   const char* openMode;
   const char *spath = normalize_path(path);

   if ((mode & LV_FS_MODE_WR) > 0 && (mode & LV_FS_MODE_RD) > 0) {
      openMode = FILE_WRITE;   // Arduino FILE_WRITE allows both read & write
   }
   else if (mode & LV_FS_MODE_WR) {
      openMode = FILE_WRITE;
   }
   else if (mode & LV_FS_MODE_RD) {
      openMode = FILE_READ;
   }

   FsFileWrap* w = new (std::nothrow) FsFileWrap();
   if (!w) return nullptr;
   w->f = SD_MMC.open(spath, openMode);
   if (!w->f) { delete w; return nullptr; }
   return w;
}


/********************************************************************
 * @brief fs_close_cb - LVGL file system callback
 */
lv_fs_res_t fs_close_cb(lv_fs_drv_t*, void* file_p) 
{
   if (!file_p) 
      return LV_FS_RES_INV_PARAM;

   FsFileWrap* w = static_cast<FsFileWrap*>(file_p);
   w->f.flush();
   w->f.close();
   delete w;
   return LV_FS_RES_OK;
}


/********************************************************************
 * @brief fs_read_cb - LVGL file system callback
 */
lv_fs_res_t fs_read_cb(lv_fs_drv_t*, void* file_p, void* buf, uint32_t btr, uint32_t* br) 
{
   if (!file_p || !buf || !br) 
      return LV_FS_RES_INV_PARAM;

   FsFileWrap* w = static_cast<FsFileWrap*>(file_p);
   *br = (uint32_t)w->f.read((uint8_t*)buf, btr);
   return LV_FS_RES_OK;
}


/********************************************************************
 * @brief fs_write_cb - LVGL file system callback
 */
lv_fs_res_t fs_write_cb(lv_fs_drv_t*, void* file_p, const void* buf, uint32_t btw, uint32_t* bw) 
{
   if (!file_p || !buf || !bw) 
      return LV_FS_RES_INV_PARAM;

   FsFileWrap* w = static_cast<FsFileWrap*>(file_p);
   size_t n = w->f.write((const uint8_t*)buf, btw);
   *bw = (uint32_t)n;
   return (n == btw) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}


/********************************************************************
 * @brief fs_seek_cb - LVGL file system callback
 */
lv_fs_res_t fs_seek_cb(lv_fs_drv_t*, void* file_p, uint32_t pos, lv_fs_whence_t whence) 
{
   if (!file_p) 
      return LV_FS_RES_INV_PARAM;

   FsFileWrap* w = static_cast<FsFileWrap*>(file_p);
   uint32_t base = (whence == LV_FS_SEEK_CUR) ? w->f.position()
                     : (whence == LV_FS_SEEK_END) ? w->f.size()
                     : 0;
   return w->f.seek(base + pos) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}


/********************************************************************
 * @brief fs_tell_cb - LVGL file system callback
 */
lv_fs_res_t fs_tell_cb(lv_fs_drv_t*, void* file_p, uint32_t* pos) 
{
   if (!file_p || !pos) 
      return LV_FS_RES_INV_PARAM;

   *pos = (uint32_t)static_cast<FsFileWrap*>(file_p)->f.position();
   return LV_FS_RES_OK;
}


/********************************************************************
 * @brief fs_dir_open_cb - LVGL file system callback
 */
void * fs_dir_open_cb(lv_fs_drv_t *drv, const char *path) 
{
   (void)drv;                             // not used!

   const char *spath = normalize_path(path);

   File d = SD_MMC.open(spath, FILE_READ);
   if (!d || !d.isDirectory()) {
      if (d) d.close();
      return nullptr;
   }
   FsDirWrap *w = new (std::nothrow) FsDirWrap();
   if (!w) { 
      d.close(); 
      return nullptr; 
   }

   w->dir = d;
   w->next = w->dir.openNextFile();   // prime the iterator
   return w;                          // LVGL will pass this back to read/close
}



/********************************************************************
 * @brief fs_dir_read_cb - LVGL file ssystem callback
 */
lv_fs_res_t fs_dir_read_cb(lv_fs_drv_t*, void* rddir_p, char* fn, uint32_t fn_len) 
{
   if (!rddir_p || !fn || fn_len == 0) 
      return LV_FS_RES_INV_PARAM;

   FsDirWrap* w = static_cast<FsDirWrap*>(rddir_p);

   if (!w->next) {
      fn[0] = '\0';   // end of directory, LVGL expects empty string
      return LV_FS_RES_OK;
   }

   // Extract file name (strip leading path if present)
   String name = w->next.name();
   int slash = name.lastIndexOf('/');
   if (slash >= 0 && slash + 1 < (int)name.length()) {
      name = name.substring(slash + 1);
   }
   name.toCharArray(fn, fn_len);

   // Advance to next entry
   w->next.close();
   w->next = w->dir.openNextFile();

   return LV_FS_RES_OK;
}


/********************************************************************
 * @brief fs_dir_close_cb - LVGL file system callback
 */
lv_fs_res_t fs_dir_close_cb(lv_fs_drv_t*, void* rddir_p) 
{
   if (!rddir_p) return LV_FS_RES_INV_PARAM;

   FsDirWrap* w = static_cast<FsDirWrap*>(rddir_p);

   if (w->next) w->next.close();
   if (w->dir)  w->dir.close();

   return LV_FS_RES_OK;
}


/********************************************************************
 * @brief Open file (low level)
 * @param path - path/filename of file to open
 * @param mode - "r" (read), "w" (write), "a" (append)
 * @param - create_new. If true a new file will be created.
 * @return File object, or NULL if failure
 */
File SD_FILE_SYS::fopen(const char* path, const char *mode, bool create_new)
{
   const char *spath = normalize_path(path); 
   File _file = SD_MMC.open(spath, mode, create_new);
   return _file;
}


/********************************************************************
 * @brief Write to an already opened file
 * @param _file - file object from previously opened file.
 * @param data - pointer to source data
 * @param len - number of bytes to write
 * @return true if successful
 */
bool SD_FILE_SYS::fwrite(File &_file, uint8_t *data, uint32_t len)
{
   uint32_t bytes_written;

   bytes_written = _file.write(data, len);
   return (bytes_written == len);
}


/********************************************************************
 * @brief Read from an already opened file
 * @param _file - file object from previously opened file.
 * @param data - pointer to data destination
 * @param len - number of bytes to read
 */
int32_t SD_FILE_SYS::fread(File &_file, uint8_t *data, uint32_t len, uint32_t offset)
{
   if(!_file.seek(offset))
      return -1;
   return _file.readBytes((char *)data, len);
}


/********************************************************************
 * @brief Close a previously opened file.
 * @param _file - File object of previously opened file.
 */
void SD_FILE_SYS::fclose(File &_file)
{
   _file.close();
}


/********************************************************************
 * @brief Return size of the file 'filename'.
 * @param file_p - pointer to path/filename string.
 * @param size - caller pointer to variable to write file size.
 * @return true if successful.
 */
int32_t SD_FILE_SYS::fsize(const char* filename) 
{
   int32_t fsz;
   if (!filename)                  // validate params
      return false;

   const char *spath = normalize_path(filename);      

   File f = SD_MMC.open(spath, FILE_READ);
   if(!f) 
      return false;

   fsz = f.size();
   f.close();
      // Serial.printf("file %s = size=%d\n", spath, fsz);
   return fsz;
}


/********************************************************************
 * @brief Write data macro, opens file for writing, writes, and closes.
 * @param path - full path & filename of file to write
 * @param data - ptr to bytes to write
 * @param len - # bytes to write. If 0, derive len from string.
 * @return - true if successful
 */
bool SD_FILE_SYS::writeFile(const char *path, uint8_t *data, uint32_t len)
{
   uint32_t ret;
   const char *spath = normalize_path(path);   

   File f = SD_MMC.open(spath, FILE_WRITE, true);  // create new file if none exists
   if(!f) {
      Serial.println("Failed to open file for writing.");
      return false;
   }
   if(len == 0)
      len = strlen((char *)data) +1;  // assume data is a c string and add 1 for null term
   ret = f.write(data, len);
   f.close();
   return (ret == len); 
}


/********************************************************************
 * @brief readFile - open a file and read contents to 'ptr'
 * @param path - char * to path/filename
 * @param len  - number of bytes to transfer. If 0 - read all bytes.
 * @param offset - file offset (seek pos)
 * @param ptr - memory pointer to callers buffer.
 * @return - number of bytes read
 */
uint32_t SD_FILE_SYS::readFile(const char * path, uint32_t len, uint32_t offset, uint8_t *ptr)
{
   uint32_t bytes_read;
   const char *spath = normalize_path(path);

   File f = SD_MMC.open(spath);
   if(!f) 
      return false;

   if(len == 0)
      len = f.available();
   f.seek(offset);
   if(ptr != NULL && len > 0) {
      bytes_read = f.readBytes((char *)ptr, len);
   }
   f.close();
   return bytes_read; 
}


/********************************************************************
 * @brief Rename a file.
 * @param pathFrom - current name of the file.
 * @param pathTo - new name of the file.
 * @return - true if successful
 */
bool SD_FILE_SYS::frename(const char *pathFrom, const char *pathTo)  // remove (delete) file
{
   return SD_MMC.rename(pathFrom, pathTo);
}


/********************************************************************
 * @brief Check if file exists.
 * @param filename - pointer to path/filename of file to check.
 * @return true if successful
 */
bool SD_FILE_SYS::fexists(const char* filename) 
{
   if (!filename) 
      return false;

   const char *spath = normalize_path(filename);
   if(!SD_MMC.exists(spath)) 
      return false;

   return true;
}


/********************************************************************
 * @brief Make new directory. 
 * @param dirname - path/name of new directory to create.
 * @return true if successful
 */
bool SD_FILE_SYS::fmkdir(const char *dirname)
{
   // Accepts "S:/foo"
   if (!dirname)  
      return false;

   const char *spath = normalize_path(dirname);

   if (SD_MMC.mkdir(spath)) {
      Serial.printf("Directory %s created.\n", spath);
   } else {
      Serial.printf("Failed to create %s (already exists or error)!\n", spath);
      return false;
   }
   return true;
}


/********************************************************************
 * @brief Remove named directory. 
 * @param dirname - name of the directory to remove.
 * @return - true if successful
 */
bool SD_FILE_SYS::frmdir(const char *dirname) 
{
   // Accepts "S:/foo"
   if (!dirname) 
      return false;

   const char *spath = normalize_path(dirname);
   if(!SD_MMC.rmdir(spath)) {
      Serial.printf("Failed to remove directory '%s'!\n", spath);
      return false;
   }
   return true;
}


/********************************************************************
 * @brief Remove the named file (delete). This function isn't included 
 * in the lvgl file functions so this mimics the lvgl style.
 * @param filename - path/name of file to remove.
 * @return - true if successful
 */
bool SD_FILE_SYS::fremove(const char* filename)
{
   if (!filename)
      return false;

   const char *spath = normalize_path(filename);   // insure proper file prefix
   if(!SD_MMC.remove(spath)) {
      Serial.printf("Failed to remove file '%s'!\n", spath);
      return false;
   }
   return true;
}


/********************************************************************
 * @brief File APPEND macro writes data to the end of the file.
 * @param path - pointer to path/file name.
 * @param data - pointer to source data buffer.
 * @param len - number of bytes to write.
 * @return true if successful
 */
bool SD_FILE_SYS::appendFile(const char *path, uint8_t *data, uint32_t len)
{
   const char *spath = normalize_path(path);
   File file = SD_MMC.open(spath, FILE_APPEND, false); // don't create new file
   if(!file) 
      return LV_FS_RES_INV_PARAM;
   
   uint32_t ret = file.write(data, len);
   file.close();
   return (ret == len);
}


/********************************************************************
 * Return a pointer to a c-string stored in PSRAM. This function reads 
 * the JSON file and extracts the desired item using category / item.
 * @param filename - path & name of the JSON file
 * @param category - Example: "wifi"
 * @param item - Example: "ssid"
 * @return pointer to the string associated with the category/item pair.
 */
char * SD_FILE_SYS::readJsonFile(const char *filename, const char *category, const char *item)
{
   JsonDocument doc;
   const char *spath = normalize_path(filename);
   File f = SD_MMC.open(spath, "r");   // open the JSON file
   if (!f) return NULL;

   // deserialize the JSON file. Exit if error
   DeserializationError err = deserializeJson(doc, f);   // read & deserialize
   f.close();
   if (err) return NULL;

   const char *str = doc[category][item] | "";
   strncpy(config_str, str, 1024);      // save string to PSRAM memory

   return config_str;
}


/********************************************************************
 * @brief Initialize card for formatting. Helper for formatting.
 */
bool SD_FILE_SYS::sdmmc_init_card(void)
{
   esp_err_t err;
   sdmmc_host_t host = SDMMC_HOST_DEFAULT();
   host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

   sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
   slot.width = SDMMC_BUS_WIDTH;
   slot.clk = (gpio_num_t) PIN_MMC_CLK;
   slot.cmd = (gpio_num_t) PIN_MMC_CMD;
   slot.d0  = (gpio_num_t) PIN_MMC_D0;
   slot.d1  = (gpio_num_t) PIN_MMC_D1;
   slot.d2  = (gpio_num_t) PIN_MMC_D2;
   slot.d3  = (gpio_num_t) PIN_MMC_D3;

   sdmmc_host_deinit();
   ESP_ERROR_CHECK(sdmmc_host_init());
   ESP_ERROR_CHECK(sdmmc_host_init_slot(host.slot, &slot));

   _sd_card = (sdmmc_card_t*)calloc(1, sizeof(sdmmc_card_t));
   err = sdmmc_card_init(&host, _sd_card);

   return (err == ESP_OK);
}


/********************************************************************
 * @brief Formatting function - force Fat32 format to SD Card
 */
esp_err_t SD_FILE_SYS::force_mkfs_fat(uint32_t au_bytes)
{
   const BYTE pdrv = 0;   // drive number “0:”
   FATFS* fs = nullptr;

   // Map “0:” to a temporary VFS path so f_mkfs can run
   esp_err_t err = esp_vfs_fat_register(TMP_MKFS_PATH, "0:", 8, &fs);
   if(err != ESP_OK) {
      Serial.printf("Failed 'esp_vfs_fat_register' code=%d\n", err);
      return err;
   }

   ff_diskio_register_sdmmc(pdrv, _sd_card);   // attach SDMMC to drive 0

   // Working buffer (max sector size); FF_MAX_SS is defined by FatFs config
   BYTE workbuf[FF_MAX_SS];

   // Old FatFs (no MKFS_PARM): f_mkfs("0:", opt, au_sectors, work, len)
   // New FatFs (has MKFS_PARM) also accepts the legacy signature in many cores,
   // but to be safe we guard it. If MKFS_PARM is not defined, use legacy call.
   FRESULT fr;

#if defined(MKFS_PARM)
   MKFS_PARM opt = {};
   opt.fmt = FM_FAT32;                    // let FatFs choose FAT16/32 by size (FAT32 for SDHC/SDXC)
   opt.au_size = au_bytes / 512;          // AU in sectors (power-of-two sectors)
   fr = f_mkfs("0:", &opt, workbuf, sizeof(workbuf));
#else
   BYTE opt = FM_FAT32;                   // force Fat32 format
   UINT au  = au_bytes;                   // power-of-two, ex: 16384
   fr = f_mkfs("0:", opt, au, workbuf, sizeof(workbuf));
#endif

   // Unmount the temporary drive
   f_mount(NULL, "0:", 0);
#ifdef HAS_DISKIO_UNREGISTER
   ff_diskio_unregister(pdrv);       // available on IDF 5.x
#endif
   esp_vfs_fat_unregister_path(TMP_MKFS_PATH);

   return (fr == FR_OK) ? ESP_OK : ESP_FAIL;
}


/********************************************************************
 * @brief Format the SD Card with (hopefully) Fat32 and write a
 * empty wattiz_config.json.
 */
bool SD_FILE_SYS::formatDrive(void)
{
   /**
    * @brief Initialize the Card
    */
   if(!isInserted()) {
      Serial.println(F("Format Error: Card not inserted!"));
      return false;
   }

   if(!sdmmc_init_card()) {
      Serial.println(F("Format Error: Failed to Init SD Card!"));
      return false; 
   }
   sdmmc_card_print_info(stdout, _sd_card);
   esp_err_t err = force_mkfs_fat(16384);
   if(err != ESP_OK) {
      Serial.printf("Format Error: %d\n", err);
      return false;
   }
   Serial.println("Formatting complete - rebooting now...");
   vTaskDelay(500);
   ESP.restart();                         // reboot to restore file system
   return true;                           // make compiler happy
}


/********************************************************************
 * @brief Reset SD Card by applying a short power cycle.
 * All MMC signals are first tristated. 
 * On exit, bus remains tristated.
 */
void SD_FILE_SYS::deInit(void)
{
   SD_MMC.end();                       // unmount the card - sudo reset

   // SD Card must now be uninitialized
   sd_card_info.error_code = SD_UNINITIALIZED; // not initialized now!
}


/********************************************************************
 * @brief SD Card write speed test.
 * @NOTE: Simple speed test measures transfer speed during writing and 
 * reading a total of 1MB on the SD card.
 * The WRITE algorithm involves writing a 1MB file sequentially.
 * The READ algorithm randomly reads 16K chunks at random offsets within the 
 * 1MB file. Random offsets are done to defeat the SD card cache and give more 
 * realistic results when doing random reads.
 * NOTE: Smaller chunk sizes will significantly decrease the read speed due to
 * more library overhead vs actual transfer.
 * @NOTE: Returns pointer to sd_speed_t struct if successful, null otherwise.
 */
sd_speed_t * SD_FILE_SYS::speedTest(uint8_t test_mode)
{
   uint16_t i;
   uint32_t speed_accum = 0;    // 1Mb evenly divisible by 512
   uint32_t tmo, offset;
   uint32_t bytes_written;
   uint32_t bytes_read;
   static sd_speed_t sd_speed;
   sd_speed.wr_mbs = 0.0;
   sd_speed.rd_mbs = 0.0;

#define SD_TEST_FILE          "/sd_speed_test.bin"
#define SPEED_CHUNK_SIZE            16384       // read file chunk size
#define NUM_CHUNKS            64          // num of chunks to equal a 1MB file
   uint32_t total_xfr_size = SPEED_CHUNK_SIZE * NUM_CHUNKS;

   // make sure card is inserted
   if(!isInserted()) {
      return NULL;
   }
   // create a test buffer in SRAM
   uint8_t *chunk_bufr = (uint8_t *)heap_caps_malloc(total_xfr_size, MALLOC_CAP_SPIRAM); 
   // uint8_t *chunk_bufr = (uint8_t*) heap_caps_malloc(SPEED_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   
   if(!chunk_bufr) {
      return NULL;
   }

   /**
    * @brief Test write speed
    */
   File file = SD_MMC.open(SD_TEST_FILE, "w", true);  // create writing file
   if(!file) {
      free(chunk_bufr);
      return NULL;
   }
      
   tmo = millis();
   bytes_written = file.write(chunk_bufr, total_xfr_size);
   if(bytes_written == 0) {
      file.close();
      free(chunk_bufr);
      return NULL;
   }
   // get write overhead in ms
   tmo = (millis() - tmo);    // add to accum
                  // Serial.printf("total_xfr_size = %d bytes, tmo = %dms\n", total_xfr_size, tmo);
   // calculate write speed in megabytes per second
   sd_speed.wr_mbs = (float(total_xfr_size) / float(tmo)) / 1000;
   file.close();
  
   /**
    * @brief Test read speed 
    */
   file = SD_MMC.open(SD_TEST_FILE, "r");   
   if(file) {
      speed_accum = 0;
      for(i=0; i<NUM_CHUNKS; i++) {
         offset = random(0, NUM_CHUNKS-1);   // randomize offset to null cache effect
         offset = offset * SPEED_CHUNK_SIZE;
         tmo = micros();
         file.seek(offset);
         if(file.readBytes((char *)chunk_bufr, SPEED_CHUNK_SIZE) == 0) {
            file.close();
            free(chunk_bufr);
            return NULL;
         }
         speed_accum += (micros() - tmo);    // add to accum
      }
            // Serial.printf("raw speed_accum = %d\n", speed_accum);      
      // speed_accum = speed_accum / NUM_CHUNKS;   // avg read time in us
      speed_accum = speed_accum / 1000;   // us -> ms
            // Serial.printf("avg speed_accum = %d\n", speed_accum);
      sd_speed.rd_mbs = (float(total_xfr_size) / float(speed_accum)) / 1000;      
      file.close();         
   } else {
      free(chunk_bufr);
      return NULL;      
   }
   // Delete test file and exit
   SD_MMC.remove(SD_TEST_FILE);
   free(chunk_bufr);
   
   return &sd_speed;
}