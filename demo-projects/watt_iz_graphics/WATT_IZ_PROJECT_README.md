# Watt-IZ Graphics Demo Project
The goal of this project is to demonstrate the features and capabilities of the
Watt-IZ Speech Enabled Development Board. A graphical user interface (GUI) is 
built using the following two graphics libraries:
- TFT_eSPI handles the low level functions such as drawing, refresh, touch screen
  detection, and many other behind-the-scenes functions.
- LVGL (Light and Versatile Graphics Library) is built on top of the TFT_eSPI
  library and provides graphical widgets to build a user friendly GUI.

Initialization of these two libraries can be found in ***gui.cpp*** in the function
**guiInit()**. Also note that this function allocates two drawing buffers in PSRAM
which significantly reduces the use of SRAM. A small *dma_linebuf* is created in SRAM
to speed up drawing.

## Hardware Requirements
- Working Watt-IZ board version 1.2 or higher.
- Functional 2.8" LCD with either capacitive or resistive touch screen.

## Page 2: Display of Miscellaneous Graphic Widgets
This page displays several graphic widgets such as buttons, switches, sliders, and
a roller. See LVGL documentation [here:] https://docs.lvgl.io/master/ .

## Page 3: Drag and Drop Demo
The goal of this page is to display a button that can be **dragged** and **dropped**. 
Press the button and drag it around the screen. When the button is released the
button text will show the X/Y coordinates where it was dropped. Note that X/Y coordinates 
represent the upper left corner of the button object.
Code example can be found in ***gui.cpp***.

## Page 4: QR Code Demo
The LVGL library contains a 3rd party library for generating and displaying a QR code.
The QR code in this demo is created from a supplied text string: 
`//https://github.com/johnny49r?tab=repositories`.
Note that the QR code being displayed is very dark, this is because when a phone camera 
tries to image the QR code, the camera is often saturated and has difficulty reading the image.





