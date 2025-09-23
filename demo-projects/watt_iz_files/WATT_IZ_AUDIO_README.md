# Watt-IZ Audio Demo Project
The goal of this project is to validate and demonstrate the audio features of 
the Watt-IZ Speech Enabled Development Board.

## Hardware Requirements
- Functional 2.8" LCD with either capacitive or resistive touch screen. 
- Small 4 ohn speaker connected to the SPKR pins. 

## Microphone Specifications
The microphone used in the Watt-IZ hardware is a TDK ICS-43434 MEMS device with
a I2S serial digital interface. The datasheet can be found [here:] https://invensense.tdk.com/wp-content/uploads/2016/02/DS-000069-ICS-43434-v1.2.pdf .

## Audio Speaker Amplifier
The audio speaker amplifier is a high efficiency Class-D 3W amplifier (MAX-98357)
using a I2S digital serial interface. Best performance is had using a 4-Ohm 
miniature speaker. Datasheet can be seen [here:] https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf .

## Tone Demo Page
The tone demo page can generate a sinewave tone from 0 to 3000 Hz. There is also a 
volume control from 0 - 100% of "maximum".
Click 'Tone ON' button to hear the selected tone and volume.

## FFT Chart Page

