# Watt-IZ Graphics Demo Project
The goal of this project is to provide working example code showing the basics 
of initializing the graphics engine for use with the Watt-IZ Speech Enabled 
Development Board.

## Touch Screen Types and Configuration
The Watt-IZ hardware can accomodate either the typical resistive touch screen, or
a capacitive touch screen. The Watt-IZ ships with a capacitive type touch.

**Capacitive** touch screens are more sensitive and are more like the feel of a 
smartphone screen and can also detect two touch areas simultaneously for appliccations
that might implement zooming. 
**Resistive** touch screens work well but require a firmer touch pressure to be detected.
Resistive touch screens also can only detect one touch area and they require calibration 
to accurately record touch position.

## Graphical User Interface (GUI)
A modern intuitive GUI can be built using the following two graphics libraries:
- **lovyanGFX** handles the low level functions such as drawing, refresh, touch screen
  detection, and many other behind-the-scenes functions.
- **LVGL** (Light and Versatile Graphics Library) is built on top of the low level graphics
  library and provides graphical widgets to build a user friendly GUI.

## Hardware Requirements
- Working Watt-IZ board revision 1.3 or higher.
- Functional 2.8" LCD with either capacitive or resistive touch screen.

## Page 1: Demo Home Screen
Introduction page.

## Page 2: Display of Miscellaneous Graphic Widgets
This page displays several graphic widgets such as buttons, switches, sliders, and
a roller. Full LVGL documentation can be found here: https://docs.lvgl.io/master/ .

## Page 3: Drag and Drop Demo
The goal of this page is to display a button that can be **dragged** and **dropped**. 
Press the button and drag it around the screen. When the button is released the
button text will show the X/Y coordinates where it was dropped. Note that X/Y coordinates 
are relative to the upper left corner of the button object.

## Page 4: QR Code Demo
The LVGL library contains a 3rd party library for generating and displaying a QR code.
The QR code in this demo is created from a supplied text string: 
`//https://github.com/johnny49r?tab=repositories`.
Note that the QR code being displayed is very dark, this is because when a phone camera 
tries to image the QR code, the camera is often saturated and has difficulty reading the image.

## Page 5: Resistive Touch Screen Calibration 
Resistive touch screens require calibration which consists of touching the four corners of
the display with a stylis. This has no effect on capacitive touch screens and is only required 
if an LCD using a resistive touch screen is used in place of the default capacitive touch screen.

## Full Project
The complete VSCode project with source code, documents, and configuration files are contained on the SD card 
distributed with the Watt-IZ basic kit. See here: https://www.tindie.com/products/abbycus/watt-iz-speech-enabled-embedded-hardware/ .


