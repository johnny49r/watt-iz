# Watt-IZ Power Demo Project
The goal of this project is to provide working example code showing the basics 
of initializing the graphics engine and displaying battery voltage and battery charge current
on a line chart. 

## Hardware Requirements
- Working Watt-IZ board revision 1.3 or higher.
- Functional 2.8" LCD with either capacitive or resistive touch screen.
- A single cell lithium Ion battery, nominal voltage of 3.7V. The recommended capacity of
the battery should be between 1500 and 3000 milliamp hours. Battery should be connected to
the -BAT+ connector and the power select jumper should be set over the BAT position.

## Page 1: Demo Home Screen
Introduction page. Swipe right to view next page.

## Page 2: Power Display
The power page displays two values on a line chart. The first value (blue color) shows battery
voltage. The second value (in red) shows the charge current. These values are updated once every 
10 seconds. Battery voltage and current are only meaningful if a battery is properly connected and 
the battery is being charged. 

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .


