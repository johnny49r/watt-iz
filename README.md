# Watt-IZ Project

## Purpose-Built Embedded Hardware for Out-of-the-Box Speech-Enabled Applications
Watt-IZ is a programmable ESP32-S3 based platform designed specifically for speech-enabled embedded applications, 
combining purpose-built hardware with a comprehensive, ready-to-use software stack. In addition to audio input 
and output, touchscreen display, SD card storage, wireless connectivity, real-time clock, and power management, 
Watt-IZ ships with a rich collection of preloaded speech and audio demos, full source code, documentation, 
and reference designs. This allows users to run and explore real speech-interactive applications immediately without 
writing code while still providing a solid foundation for custom development.

The Watt-IZ exists for people who want to explore voice interfaces, conversational systems, language translators, 
chatbot’s, voice assistants, command interpreters, and many other AI-style applications using real hardware 
without needing a full development system or complex setup just to run and experience the speech-enabled features 
of the device. Demos and applications can operate standalone from the SD card, while users who want to build or 
modify firmware can use a normal PC-based development workflow.

## Hardware Features
The Watt-IZ project hardware is packaged on a 100mm x 60mm PCB. See PCB section for mechanical details. The
following describes the hardware capabilities of each major section. 

### CPU
  - Espressif ESP32-S3 SOC, 32-bit LX7 dual core @ clock speed up to 240 MHz. 
  - 16MB flash (program) memory, 512KB SRAM, 8MB of PSRAM (pseudo static RAM).
  - WiFi connectivity supports 2.4GHz, IEEE 802.11/b/g/n. Built-in WiFi PCB antenna.
  - WiFi can operate in Station mode (STA), Access Point mode (AP), or both.
  - Bluetooth Low Energy (BLE 5.0) supports high speed, long distance, and low power operation.
  - ESP-NOW proprietary connection-less low power wireless protocol for direct device-to-device communication
    using MAC addressing. Used for streaming audio and local control functions.
  - Peripherals: PWM, I2C, UART, SPI, SDIO, IrDA, GPIO, LCD, Camera interface, UART, I2S, USB 1.1 OTG,
    USB Serial/JTAG controller, MCPWM, SDIO host interface, GDMA, TWAI® controller (compatible with ISO 11898-1),
    12-bit ADC, touch sensor, temperature sensor, timers, and watchdog.
  - DSP optimized routines for FFT / IFFT, FIR / IIR filters, convolution, matrix math, and windowing functions.
### Audio Input/Output
  - Embedded I2S Mems microphone with 16 bit, 16KHz sampling rate.
  - High efficiency class D I2S audio amplifier and speaker driver. Optimized for 4 ohm 5W miniature speakers.
### Display and User Interface
  - 2.8” IPS LCD with modern, high sensitivity capacitive touch screen. SPI serial interface up to 40MHz. 320X240 pixels with 16 bit color (65K colors).
  - Wake button configured to act as a wake-from-deep-sleep trigger or as a general purpose function button.
  - Intelligent programmable full spectrum LED for status and error notification. 24 bit color depth and 255 levels of brightness.
  - Utilizes the open source LVGL graphics library for event driven UI development. Driver supports DMA transfers to enhance performance.
### Storage and Data
  - SD-MMC (4-bit mode) SDXC/HC microSD card 32GB. 
  - Employs the FAT32 file system with full compatibility to the LVGL library file functions.
  - Apps and demo programs can be loaded directly from the SD card – no need to download via serial port.
### Power Management
  - The device is externally powered from a typical 5V AC adapter (phone charger) with a USB-C connector. Additionally power can
    be supplied from the micro-USB connector used for development and debug.
  - Full support for battery operation from a single cell lithium-ion battery, 1000 – 3000 MAh capacity. Charging current 900 Ma.
  - The device can operate at full power for a minimum of 4 hours using a 2000 Mah battery. 
  - Battery battery voltage and charging current are monitored by the CPU’s Analog-to-Digital Converter.
### Real Time Clock
  - High accuracy Real Time Clock (RTC) chip with coin cell backup battery for years of continuous timekeeping. Allows
    accurate time preservation across power failure without the need for manual setting or internet time sync after power up.
  - Real-Time Clock Counts Seconds, Minutes, Hours, Date of the Month, Month, Day of the Week, and Year, with Leap-Year Compensation.

## Software Demonstrations
### Overview
Watt-IZ supports a large and growing collection of demonstration programs, user documentation, and a core “Watt-IZ” application
that serves to encapsulate many of the platform’s capabilities. Each demo program has an associated README file describing 
the purpose and operation of the demo. All demo’s and the core Watt-IZ application can be loaded directly from the SD card 
(see Downloading Firmware section) without requiring a host development system, download cable, etc.

All demo's and apps require functional Watt-IZ hardware, especially the graphics display and a functioning SD card. Some 
require a connected speaker while others may require an API key for speech conversion, language translation, or chatbot services  
(see the readme file WATT_IZ_API_KEYS.md for details).

### GUI Functions (see demo watt_iz_graphics)
  - GUI basic widgets and touch screen.
  - Advanced drag & drop example.
  - Create and display QR codes.
  - PWM backlight LED control.
### Storage and File System (see demo watt_iz_files).
  - Format an SD card with FAT32.
  - Utility to create a system credential JSON file using a text file with your WiFi info and API keys.
  - Display file hierarchy.
  - Measure SD card write and read speeds in the hardware environment.
  - Load demo programs and apps directly from the SD card. All demo’s have code example of how to implement this feature.
  - Use case examples of Non Volatile Storage using the ArduinoNVS library (EEPROM functionality).
### Speech and Audio Functions
  - Record and playback WAV (uncompressed) files. See demo watt_iz_audio.
  - Audio processing (low pass filtering, FFT).
  - Streaming (works with ESP-NOW to stream over WiFi).
  - Speech detection (use FFT and Formant analysis to detect valid speech vs other noises).
  - Sinewave frequency and ring tone functions.
### Real Time Clock & Alarm (see demo watt_iz_clock)
  - Demonstrates timekeeping functions (year, month, day, hour, minute, second).
  - Time synchronization with internet NTP server.
  - Calender with reverse and forward month scrolling.
  - Alarm trigger example.
### AI Cloud Demo's
  - Google Speech to Text service (see demo watt_iz_STT).
  - Google Text to Speech service (see demo watt_iz_TTS).
  - Google language translate service (see demo watt_iz_translator).
  - OpenAI Chat GPT service (see demo watt_iz_chatgpt).
### Intercom Demo (Direct Peer-to-Peer Communication)
  - Uses broadcast to discover other devices.
  - Uses MAC addressing for peer-to-peer communication.
  - Demonstrates writing and reading audio streams.
### Intent Classification / Command Parsing
  - Break speech into intent – action – target categories (*** coming soon).

## Firmware Development 
USB-Serial port via a micro-USB connector. All hardware necessary to interface with an Integrated Development 
Environment (IDE) such as Visual Studio Code. 
Auto download mode is supported – no buttons to push to initiate firmware download.

## Power 
The board is designed to operate from a standard USB-C port. A typical 5V @ 2A AC power source (phone charger) can be used 
to power the board. Additionally the board can be powered from a single Lithium-Ion 3.7V battery to provide 
several hours of operation at full power operation. The battery is connected to the board via a two pin header 
located next to the USB-C connector. Note the board has reverse polarity protection in case the battery is reversed.
A 900 mA charger circuit is provided to charge the battery when the AC adapter is powering the board. A charge status 
LED indicates charging (or full). Power ON/OFF switch is located on the front edge of the board.
The battery voltage and charge current are exposed to the MCU's ADC for applications requiring power monitoring, 
battery State Of Charge, and Time To Charge calculation.

High efficiency switching regulators provide stable voltage and maximum battery life.

### More Information
See the file "Watt-IZ User Manual.pdf" contained on the SD card in each Watt-IZ basic kit.

### To Purchase
Go here: [https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/]

## MIT License
The software examples are free for use, modification, and distribution. See the LICENSE file for details. 
John F Hoeppner, Watt-IZ, Abbycus 2025.
