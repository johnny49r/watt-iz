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
Introduction page. Swipe right to view the next page.

## Page 2: Battery Power Measurement
Power measurements are derived from the system's Analog to Digital Converter (ADC). Battery 
voltage is read directly while the charge current is calculated by measuring the voltage across
the current mirror of the charge controller.

Battery voltage and charge current are represented with two values displayed on the line chart. 
The first value in the Y axis (blue color) shows the current battery voltage. 
The second value in the Y axis (red color) shows the charge current. 
The X axis of the chart represents the last 10 minutes of power samples updated once per second. 

A charge 'LED' on the lower right of the page will be RED if the battery is being charged, and GREEN
if the battery has reached 100% charge.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .


