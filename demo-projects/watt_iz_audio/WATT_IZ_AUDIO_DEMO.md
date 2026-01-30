# Watt-IZ Audio Demo Project
The goal of this project is to validate and demonstrate the audio features of 
the Watt-IZ Speech Enabled Development Board.

## Hardware Requirements
- Functional Watt-IZ hardware with the following attributes:
    - Functional 2.8" LCD.
    - Installed 32GB SD card.
- Small 4 ohm speaker connected to the SPKR pins. 

## Microphone Specifications
The microphone used in the Watt-IZ hardware is a TDK ICS-43434 MEMS device with
a I2S serial digital interface. The datasheet can be found [here:] https://invensense.tdk.com/wp-content/uploads/2016/02/DS-000069-ICS-43434-v1.2.pdf .

## Audio Speaker Amplifier
The audio speaker driver is a high efficiency Class-D 3W amplifier (MAX-98357)
using a I2S digital serial interface. Best performance is had using a 4-Ohm 
miniature 3W speaker. Datasheet can be seen [here:] https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf.

## Recorder Page
Page 2 of the Audio demo shows a typical audio voice recorder control buttons - Record - Playback -
Pause - Stop.
Just below the recorder controls is a simple progress bar indicating progress during recording or playback.
When the Record button is pressed, a 10 second recording is begun. The recording does not actually 
start until a voice has been detected by the Valid Audio Detect algorithm in the audio library.
The recording will continue until either the 10 second interval has been reached or the audio algortihm 
has not detected voice for a period of 2 seconds. 
The recording is saved as a WAV file on the SD card. The user can playback this recording by pressing the 
Playback button.
Pause and Stop buttons are active during both recording and playback.

## Tone Demo Page
Page 3 shows a demo where the audio system can generate a sinewave tone from 100 to 3000 Hz. There is also a 
volume control from 0 - 100% of "maximum".
Click 'Tone ON' button to hear the selected tone. Insure volume is set > 0.

## FFT Chart Page
Page 4 of the demo shows a line chart representing simulated tone and actual microphone data when
processed by the Fast Fourier Transform (FFT) algorithm. The chart has two options selected by a toggle 
button at the bottom of the display. 
**Simulation mode:** In this mode the software generates a continuous sinewave of frequencies ranging from 
100 to 4000 Hz. The FFT algorithm converts a time based signal (raw sinewave) to the frequency spectrum 
and displays this on the chart.
**Microphone Mode:** In this mode actual microphone audio data is processed by the FFT and displayed on
the chart. All frequency components of the voice signal can be seen on the chart.
**Note:** that the ESP32-S3 SOC contains Digital Signal Processing (DSP) hardware functions for accelerating 
FFT math, making the real time display of FFT data possible.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit.

