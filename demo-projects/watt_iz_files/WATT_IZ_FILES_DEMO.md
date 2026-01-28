# Watt-IZ FILES DEMO Project
This goal of this example project for the Abbycus Watt-IZ Speech Enabled ESP32-S3 development board demonstrates basic file system use, formatting, and configuration of the SD card storage device. Additionally a simple speed benchmark tool is available to check relative performance of real in-system performance. 

## What is Watt-IZ?
Watt-IZ is a programmable ESP32-S3 based platform designed specifically for speech-enabled embedded applications, combining purpose-built hardware with a comprehensive, ready-to-use software stack. In addition to audio input and output, capacitive touchscreen display, SD card storage, wireless connectivity, real-time clock, and power management, Watt-IZ ships with a rich collection of preloaded speech and audio demos, full source code, documentation, and reference designs. This allows users to explore, run, and modify real speech-interactive applications immediately (without writing code) while still providing a solid foundation for custom development.

## Demo Hardware Requirements
This demo requires a functional Watt-IZ platform V1.3 or higher, a 2.8" touch screen LCD, and a
functional 32GB classs 10 micro-SD card. 

## How to Download Firmware From the SD Card
Each demonstration program and Watt-IZ apps can be downloaded from the SD card without the need for a development system, download cable, etc.
Each demo and app has an associated “firmware.bin” file in their respective folders on the github repository. To download and run programs from the SD card, perform the following steps:
    - Turn Watt-IZ power off and remove the SD card from the device. 
    - Insert the SD card into a PC or suitable phone that can access FAT32 storage devices.
    - Copy the desired “firmware.bin” file to the SD card inside the “update” folder (path = “/update/firmware.bin”).
    - Remove the card from the host and replace in the Watt-IZ device.
    - Power up the device and follow the instructions in the dialog to download the new firmware.
    - Note that the firmware.bin file will be renamed as 'firmware.used' after downloading is successful to     prevent subsequent downloads of the same firmware. 

## SD File System Introduction
The SD_FILE_SYS Library is a unified C++ file system interface which wraps the native SD_MMC functions while seamlessly integrating with the LVGL file system API. This dual-purpose design allows both user 
applications and LVGL-based third-party components to share the same storage layer transparently.

**Key features include:**

* LVGL Integration: exposes SD-card access to any LVGL widget or library using the lv_fs_* interface.

* Direct SD_MMC Access: provides simplified, high-level wrappers for native SD_MMC operations.

* Utility Tools: built-in SD-card formatting, read/write speed benchmarking, and a recursive directory listing function for full storage exploration.

Ideal for projects combining ESP32 hardware, LVGL GUIs, and SD-based storage, the SD_FILE_SYS library delivers a single, consistent, and efficient interface for all file operations.

## Using the Demo 
### Page 1: Demo Logo 
Swipe to advance to the next page.

### Page 2: Directory Display
Folders & files on the SD card will be displayed on the LCD display using the LVGL graphics 
**List** widget. A **DropDown List** widget shows card capacity and card type.

### Page 3: SD Card Speed Test
A simple speed test can be used to benchmark relative performance of the SD card in the ESP32 environment.
Transfer speeds are measured by writing and reading 16Kbyte blocks for a total of 1MB transfered. 
Measured times for each block are averaged and displayed after the test has completed.

### Page 4: SD Card Formatter
The system SD card must be formatted with the FAT32 type format. If the card is not formatted or may be
formatted with a different format, use this function to format the SD card with the FAT32 format. 
***WARNING - ALL FILES will be erased!***.

### Page 4: Create New Configuration File 
Many of the demo projects require internet access and some demos/apps require API keys for Google and OpenAI (Chat GPT) access. The program read this information from a file on the SD card named ***'wattiz_config.json'***.
To build the configuration file *'wattiz_config.json'*, place your WiFi credentials and API keys in the header file "credentials.h". When ready click the **'CREATE'** button to write a new *'wattiz_config.json'*.
Note you can also directly edit the *'wattiz_config.json'* file to add/change key information.

***WARNING:*** The contents of the files *'credntials.h'* and *'wattiz_config.json'* are plain text and therefore are not secure. There are many ways to provide security for your private data but that is outside the scope of this project.

### Path / Filename Conventions
File system functions accept either a forward slash "/" or a drive letter and colon ("S:") as a prefix to path/file names. 
Examples: *S:/path/file...* or */path/file...*.

### Integrated File System for LVGL
The SD_FILE_SYS library Provides seamless access to SD-card files through the LVGL *lv_fs_* API, allowing LVGL applications to open, read, write, and browse files using standard LVGL functions while transparently using the underlying SD_MMC storage.

## SD_FILE_SYS Class Low Level Functions
The low level class functions are mostly wrappers for standard SD_MMC file functions.

### **bool isInserted(void)**
    * Checks if SD card is inserted into the SD socket. Return true if inserted.
    * Example:

    bool ret = sd.isInserted();
    if(!ret) {
        // SD card NOT inserted.
    }

### **File fopen(const char\* path, const char \*mode, bool create_new)**
    * Open (and optionally create) a file for reading, writing, or appending.
    * Example:

    // Example writing to a file:
    char wr_text[] {"Hello There!"};;
    File f = sd.fopen("S:/testfile.txt", FILE_WRITE, true); // open for write, create new
    if(!f) {
        // open failed!
    } else {
        bool ret = sd.fwrite(&f, &wr_text, strlen(wr_text));
        if(!ret) {
            // write failed!
        }
        sd.fclose(&f);
    }

    // Example reading from a file:
    char rd_text[100];
    int32_t bytes_read;
    File f = sd.fopen("S:/testfile.txt", FILE_READ, false); // open for read
    bytes_read = sd.fread(&f, &rd_text, 12);
    if(bytes_read <= 0) {
        // read failed!
    } else {
        Serial.printf("read file=%s\n", rd_text);
    }
    sd.fclose(&f);

### **bool fwrite(File &_file, uint8_t \*data, uint32_t len)**
    * Write data to a previously opened file.
    * Example: See fopen above.


### **int32_t fread(File &_file, uint8_t \*data, uint32_t len, uint32_t offset)**
    * Read data from a previously opened file.
    * Example: See fopen above.

### **bool fremove(const char \*filename)**
    * Remove (delete) a file.
    * Example:

    bool ret = sd.fremove("S:/testfile.txt");
    if(!ret) {
        // remove failed!
    }

### **bool frename(const char \*pathFrom, const char \*pathTo)**
    * Rename a file.
    * Example:

    bool ret = sd.frename("S:/testfile.txt", "newname.txt");
    if(!ret) {
        // rename failed!
    }

### **bool fmkdir(const char \*dirname)**
    * Create a new directory on the drive.
    * Example:

    bool ret = sd.fmkdir("S:/newdir");  // create new directory on the root
    if(!ret) {
        // fmkdir failed!
    }

### **bool frmdir(const char \*dirname)**
    * Remove (delete) a directory.
    * Example:

    bool ret = sd.frmdir("S:/newdir");  // delete dir
    if(!ret) {
        // frmdir failed! - bad dir name or directory doesn't exist
    }

### **bool fexists(const char \*filename)**
    * Return true if file exists.
    * Example:

    bool ret = sd.fexists("S:/testfile.txt");
    if(!ret) {
        // no file found!
    }

### **int32_t fsize(const char \*filename)**
    * Return size in bytes of the specified file.
    * Example:

    int32_t file_size = sd.fsize("S:/testfile.txt");
    if(file_size < 1) {
        // file not found or zero length file
    }

### **bool fseek(File &_file, uint32_t pos, SeekMode sm = SeekSet)**
    * Sets to file position. SeekMode option;
        * SeekSet (default) set absolute position.
        * SeekCur - seek to position relative to current position.
        * SeekEnd - seek to end of file minus 'pos'.
    * Example:

    File f = sd.fopen("S:/testfile.txt", FILE_READ, false); // open for read
    bool ret = sd.fseek(&f, 100); // set read offset to 100

### **void fclose(File &_file)**
    * Close a previously opened file.
    * Example: See fopen above.

## SD_FILE_SYS Class Macro Functions
Macro functions are convenience functions that wrap several low level functions to 
perform common tasks. Example: writeFile conflates fopen, fwrite, and fclose to perform
a file write via a single function call.

### **bool writeFile(const char \*path, uint8_t \*data, uint32_t len)** 
    * Macro function writes data to a file. If file doesn't exist, a new file is created.
    * Example:

    char text[] = {"Hey hello!"};
    bool ret = sd.writeFile("S:/testfile.txt", &text, strlen(text));
    if(!ret) {
        // write error!
    }

### **uint32_t readFile(const char \*path, uint32_t len, uint32_t offset, uint8_t \*ptr)**
    * Macro function reads data from a file. The value 'offset' is equivalent to seek to position.
    * Example: 

    char bufr[100];
    uint32_t bytes_read = sd.readFile("S:/testfile.txt", 12, 0, &bufr); // read from file beginning
    if(bytes_read < 1) {
        // no data read!
    }

### **bool appendFile(const char \*path, uint8_t \*data, uint32_t len)**
    * Macro function appends data to the end of a file.
    * Example:

    char added_text[] = {"Good afternoon!"};
    bool ret = sd.appendFile("S:/testfile.txt", &added_text, strlen(added_text));
    if(!ret) {
        // append failed!
    }

## The Warranty
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. @Abbycus 2026
