# Watt-IZ FFT Demo Project
The goal of this project is to demonstrate the use of the ESP32-S3 capabilites for FFT and audio filtering 
with the Watt-IZ Speech Enabled Development Board.

### White Noise Test Signal Demo
White noise is used as the input signal to provide broadband spectral content across the entire frequency 
range. This enables direct observation of FFT magnitude output and precise evaluation of low-pass filter 
characteristics, including roll-off behavior and Q-dependent response near the cutoff frequency.

Expected behavior:
- FFT output shows approximately uniform magnitude across frequencies.
- Increasing cutoff frequency passes more high-frequency energy.
- Increasing Q factor sharpens the transition and may introduce spectral peaking near cutoff.

This demo is intended for validation and visualization of DSP behavior rather than perceptual audio quality.

## Hardware Requirements
- Functional Watt-IZ hardware with the following attributes:
    - Functional 2.8" LCD.
    - Installed 32GB SD card.

## Demonstration 
The demo page consists of a chart that will show a plot of audio signal attenuation for a given cutoff frequency
and a specified Q factor. Operation:
- Select a cutoff frequency between 0 and 8 KHz. 
- Select a Q factor (filter damping value).
- Click the **FFT Scan** button and view the attenuation curve of the selected values.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit.

