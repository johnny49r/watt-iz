/********************************************************************
 * sdcard.cpp
 */
#include "sdcard.h"

SD_CARD_MMC sdcard;

File _file;


/********************************************************************
 * @brief Construct a new SD_CARD_MMC object
 */
SD_CARD_MMC::SD_CARD_MMC(void) 
{
   sd_card_info.error_code = SD_UNINITIALIZED;
   _cred_str = nullptr;
}


/********************************************************************
 * @brief Reset SD Card by applying a short power cycle.
 * All MMC signals are first tristated. 
 * On exit, bus remains tristated.
 */
void SD_CARD_MMC::unmount(uint32_t power_delay)
{
   SD_MMC.end();                       // unmount the card - sudo reset

   // SD Card must now be uninitialized
   sd_card_info.error_code = SD_UNINITIALIZED; // not initialized now!
}


/********************************************************************
 * Returns true if SD Card is inserted 
 */
bool SD_CARD_MMC::isInserted(void)
{
   pinMode(PIN_SD_CARD_DETECT, INPUT_PULLUP);
   vTaskDelay(10);
   return (digitalRead(PIN_SD_CARD_DETECT) == LOW);
}


/********************************************************************
 * @brief Initialize the SD Card using 4 bit MMC bus.
 */
bool SD_CARD_MMC::init(void)
{
   if(!isInserted()) {                      // is card inserted?
      Serial.println("ERROR: SD Card not inserted!");
      sd_card_info.error_code = NOT_INSERTED;
      return false;
   }
   // unmount(50);                        // power reset 
   SD_MMC.setPins(PIN_MMC_CLK, PIN_MMC_CMD, PIN_MMC_D0, PIN_MMC_D1, PIN_MMC_D2, PIN_MMC_D3);
   if (!SD_MMC.begin()) {                 // MMC 4-bit mode, no format on fail, 40 MHz speed
      sd_card_info.error_code = MOUNT_FAILED;
      return false;
   }

   // initialize file system lib and link to SD_MMC
   static FS* fs = &SD_MMC;   // point at your FS

   sd_card_info.card_type = SD_MMC.cardType();
   sd_card_info.card_size = SD_MMC.cardSize();
   sd_card_info.bytes_avail = SD_MMC.totalBytes();
   sd_card_info.bytes_used = SD_MMC.usedBytes();

   // Create buffer for saving decode of JSON files
   _cred_str = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM); 
   
   sd_card_info.error_code = (sd_card_info.bytes_avail > 0) ? SD_OK : CARD_FULL;
   return (sd_card_info.error_code == SD_OK);
}


/********************************************************************
 * @brief Copy card info into the callers structure.
 */
void SD_CARD_MMC::getCardInfo(sd_card_info_t *card_info)
{
   memcpy(card_info, &sd_card_info, sizeof(sd_card_info_t));
}


/********************************************************************
 * @brief Create a new directory.
 * 
 * @param path 
 */
bool SD_CARD_MMC::makeDir(const char * path)
{
   return SD_MMC.mkdir(path);
}


/********************************************************************
 * @brief Verify if a file already exists.
 * 
 * @param path - full path + filename
 * @return true if file exists
 */
bool SD_CARD_MMC::fileExists(const char * path)
{
   return SD_MMC.exists(path);
}


/********************************************************************
 * @brief return size of the specified file on the SD card
 * 
 * @param path - path + filename
 * @return uint32_t - size of file in bytes
 */
uint32_t SD_CARD_MMC::getFileSize(const char *path)
{
   uint32_t len = 0;

   File file = SD_MMC.open(path);
   if(!file)
      return len;          // return 0 if file can't be opened

   len = file.size();      // get size in bytes
   file.close();
   return len;
}


/********************************************************************
 * @brief Delete the specified file
 * 
 * @param path - path + filename
 */
bool SD_CARD_MMC::deleteFile(const char * path)
{
   return SD_MMC.remove(path);
}


/********************************************************************
 * @brief Rename the specified file
 * 
 * @param path1 - current filename
 * @param path2 - new filename
 */
bool SD_CARD_MMC::renameFile(const char * path1, const char * path2)
{
   return SD_MMC.rename(path1, path2);
}


/********************************************************************
 * @brief Append data to the specified file
 * 
 * @param path - path + filename
 * @param message - chars to add to the file
 * @return true if successful
 */
bool SD_CARD_MMC::appendFile(const char * path, uint8_t *data, uint32_t len)
{
   File file = SD_MMC.open(path, FILE_APPEND);
   if(!file) return false;
   
   uint32_t ret = file.write(data, len);
   file.close();
   return true;
}


/********************************************************************
 * @brief Write to the specified file
 * 
 * @param path - full path & filename of file to write
 * @param data - ptr to bytes to write
 * @param len - # bytes to write. If 0, derive len from string.
 * @return true if successful
 */
bool SD_CARD_MMC::writeFile(const char *path, uint8_t *data, uint32_t len)
{
   uint32_t ret;
   File file = SD_MMC.open(path, FILE_WRITE, true);
   if(!file){
         // Serial.println("Failed to open file for writing.");
      return false;
   }
   if(len == 0)
      len = strlen((char *)data) +1;  // assume data is a c string and add 1 for null term
   ret = file.write(data, len);
   file.close();
   return (ret == len);
}


/********************************************************************
 * @brief readFile - open a file and read contents to 'ptr'
 * 
 * @param path - char * to path/filename
 * @param len  - number of bytes to transfer. If 0 - read all bytes.
 * @param offset - file offset
 * @param ptr - memory pointer
 */
bool SD_CARD_MMC::readFile(const char * path, uint32_t len, uint32_t offset, uint8_t *ptr)
{
   File file = SD_MMC.open(path);
   if(!file) {
      return false;
   }

   if(len == 0)
      len = file.available();
   file.seek(offset);
   if(ptr != NULL && len > 0) {
      file.readBytes((char *)ptr, len);
   }
   file.close();
   return true;
}


/********************************************************************
 * Open file for 'open_mode'. Return File object.
 */
bool SD_CARD_MMC::fileOpen(const char * filepath, const char *open_mode, bool create_new)
{
   _file = SD_MMC.open(filepath, open_mode, create_new);
   if(!_file) 
      return false;
   return true;
}


/********************************************************************
 * Close an open file
 */
void SD_CARD_MMC::fileClose(void)
{
   _file.close();
}


/********************************************************************
 * Write to a file that is already open
 */
bool SD_CARD_MMC::fileWrite(uint8_t *data, uint32_t len)
{
   uint32_t bytes_written;
   bytes_written = _file.write(data, len);
   return (bytes_written == len);
}


/********************************************************************
 * @brief Read 'n' bytes from already opened file from 'offset'
 */
int32_t SD_CARD_MMC::fileRead(uint8_t *data, uint32_t len, uint32_t offset)
{
   uint32_t bytes_read;
   if(!_file.seek(offset))
      return -1;
   bytes_read = _file.readBytes((char *)data, len);
   return bytes_read;
}


/********************************************************************
 * @brief List contents of the specified directory.
 * 
 * @param dirname = directory name, "/" == root dir
 * @param levels = depth of levels to display
 */
void SD_CARD_MMC::listDir(const char *dirname, String &output, uint8_t levels)
{
  buildDirIndex(SD_MMC, dirname, index); 
    output.clear();

    for (auto &d : index) {
      //   Serial.printf("[%s]\n", d.path.c_str());
        output += "[" + d.path + "]\n";

        // Subdirectories
        for (auto &s : d.subdirs) {
            // Serial.printf(" <DIR>  %s\n", s.c_str());
            output += "<DIR>  " + s + "\n";
        }

        // Files with correct sizes
        for (auto &f : d.files) {
            int32_t idx = f.path.lastIndexOf("/");
            String fname = f.path.substring(idx + 1);

            output += fname + ", Size: " + String(f.size) + "\n";

            // Serial.printf(" <FILE> %s \tSize: %llu\n",
            //               f.path.c_str(),
            //               (unsigned long long)f.size);
        }
    }
}


/********************************************************************
 * Build a vector of DirIndex; each entry groups its subdirs/files.
 * Set maxEntriesPerDir>0 to cap very large folders.
 */
void SD_CARD_MMC::buildDirIndex(FS& fs, const String& root, 
            std::vector<DirIndex>& out, size_t maxEntriesPerDir) 
{
   out.clear();
   std::vector<String> stack;
   stack.push_back(root);

   while (!stack.empty()) {
      String dirPath = stack.back();
      stack.pop_back();

      File dir = fs.open(dirPath, "r");
      if (!dir || !dir.isDirectory()) continue;

      DirIndex di;
      di.path = dirPath;

      dir.rewindDirectory();
      size_t count = 0;
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
         String name = f.name();   // Some FSes return name only, some full path
         bool isDir = f.isDirectory();

         // Normalize child path
         String childPath = (name.startsWith("/"))
                              ? name
                              : joinPath(dirPath, name);

         if (isDir) {
               di.subdirs.push_back(childPath);
               stack.push_back(childPath);
         } else {
               FileEntry fe{childPath, (uint64_t)f.size()};
               di.files.push_back(fe);
         }

         f.close();
         if (maxEntriesPerDir && ++count >= maxEntriesPerDir) break;
      }
      dir.close();

      // Sort subdirs
      std::sort(di.subdirs.begin(), di.subdirs.end());

      // Sort files by path
      std::sort(di.files.begin(), di.files.end(),
               [](const FileEntry &a, const FileEntry &b) {
                     return a.path < b.path;
               });

      out.push_back(std::move(di));
   }

   // Optional: sort directories by path so parent appears before children
   std::sort(out.begin(), out.end(),
            [](const DirIndex &a, const DirIndex &b) {
               return a.path < b.path;
            });
}


/********************************************************************
 * Helper - join path and filename.
 */
String SD_CARD_MMC::joinPath(const String& dir, const String& name) 
{
  if (dir == "/" || dir.length() == 0) return "/" + name;
  return dir + "/" + name;
}


/********************************************************************
 * Return a pointer to a cstring stored in PSRAM. This function reads 
 * the JSON file and extracts the desired item using category / item.
 * @param filename - path & name of the JSON file
 * @param category - Example: "wifi"
 * @param item - Example: "ssid"
 * @return pointer to the string associated with the category/item pair.
 */
char * SD_CARD_MMC::readJSONfile(const char *filename, const char *category, const char *item)
{
   JsonDocument doc;
   File f = SD_MMC.open(filename, "r");
   if (!f) return NULL;

   // deserialize the JSON file. Exit if error
   DeserializationError err = deserializeJson(doc, f);
   f.close();
   if (err) return NULL;

   const char *str = doc[category][item] | "";
   strncpy(_cred_str, str, 1024);      // save string in PSRAM memory

   return _cred_str;
}


/********************************************************************
 * @brief Initialize card for formatting 
 */
bool SD_CARD_MMC::sdmmc_init_card(void)
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
 * @brief Format function - force Fat32 format to SD Card
 */
esp_err_t SD_CARD_MMC::force_mkfs_fat(uint32_t au_bytes)
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
bool SD_CARD_MMC::formatSDCard(void)
{
   bool ret;
   /**
    * @brief Initialize the Card
    */
   if(!isInserted()) {
      Serial.println(F("Format Error: Card not inserted!"));
      return false;
   }

   ret = sdmmc_init_card();
   if(!ret) {
      Serial.println(F("Format Error: Failed to Init SD Card!"));
      return false;
   }
   sdmmc_card_print_info(stdout, _sd_card);
   esp_err_t err = force_mkfs_fat(16384);
   if(err != ESP_OK) {
      Serial.printf("Format Error: %d\n", err);
   }
   return (err == ESP_OK);
}


/********************************************************************
 * @brief Write a config file to the SD Card.
 */
bool SD_CARD_MMC::writeConfigFile(const char *filename, sys_credentials_t *creds)
{
   String out;
   // Build the JSON document 
   JsonDocument doc;
   doc.clear();
   doc["version"] = 2;
   doc["device"]["system_id"] = "Abbycus Watt-IZ";

   doc["wifi"]["ssid"]     = creds->wifi_ssid;
   doc["wifi"]["password"] = creds->wifi_password;

   doc["google"]["api_endpoint"] = creds->google_endpoint;
   doc["google"]["api_key"]      = creds->google_api_key;
   doc["google"]["api_server"]   = creds->google_server;

   doc["openai"]["api_endpoint"] = creds->openai_endpoint;
   doc["openai"]["api_key"]      = creds->openai_api_key;
   doc["openai"]["chat_version"] = creds->openai_chat_model;

   doc["updated_at"] = "Sat, Sep 20, 14:00";   // e.g. your ISO-8601 string
   
   size_t n = serializeJsonPretty(doc, out); // make sanitized JSON string
   if(n <= 0)                             // serialize fail?
      return false;
   // Write the JSON file and exit
   return writeFile(filename, (uint8_t *)out.c_str(), out.length());
}


/********************************************************************
 * @brief SD Card write speed test.
 * @NOTE: Simple speed test measures transfer speed during writing and 
 * reading a 1MB file on the SD card.
 */
sd_speed_t * SD_CARD_MMC::speed_test(uint8_t test_mode)
{
   float test_file_sz = 1000000; //1048576;
   uint32_t tmo;
   static sd_speed_t sd_speed;
   sd_speed.wr_mbs = 0.0;
   sd_speed.rd_mbs = 0.0;
#define SD_TEST_FILE          "/sdtest.bin"

   // make sure card is inserted
   if(!isInserted()) {
      return NULL;
   }

   /**
    * @brief Test write speed
    */
   if(fileOpen(SD_TEST_FILE, "w", true)) {
      // create a write buffer in PSRAM
      uint8_t *wr_bufr = (uint8_t *)heap_caps_malloc(int(test_file_sz), MALLOC_CAP_SPIRAM); 
      tmo = millis();
      if(!fileWrite(wr_bufr, int(test_file_sz))) {
         fileClose();
         free(wr_bufr);
         return NULL;
      } 
      // get write overhead in ms
      tmo = (millis() - tmo);

      // calculate megabytes per second
      sd_speed.wr_mbs = (test_file_sz / float(tmo)) / 1000;
      fileClose();

      /**
       * @brief Test read speed 
       */
      if(fileOpen(SD_TEST_FILE, "r", false)) {
         tmo = millis();
         fileRead(wr_bufr, test_file_sz, 0);
         tmo = millis() - tmo;
         fileClose();         
      } 
      if(tmo == 0) 
         sd_speed.rd_mbs = 0;
      else 
         sd_speed.rd_mbs = (test_file_sz / float(tmo)) / 1000;
      deleteFile(SD_TEST_FILE);
      free(wr_bufr);
   }
   return &sd_speed;
}