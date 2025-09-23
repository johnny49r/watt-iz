# Instructions for Installing Demo Projects
## Install Visual Studio Code and PlatformIO IDE extensions.
The Watt-IZ demo projects are Arduino style C++ code examples developed using the 
Visual Studio code editor ***(VScode)*** with the popular IDE extension ***PlatformIO***.
Install VScode and the PlatformIO IDE extension using directions here: https://platformio.org/install/ide?install=vscode.

## Copy Source from Github Repository
1) Goto the watt-iz Github project https://github.com/johnny49r/watt-iz/tree/main.
2) 

## Install Demo Project
The Watt-IZ demo projects are organized into folders that can be dropped directly into your 
project folder. Then perform the following steps:
1) In VSCode, go to the PlatformIO Home (alien icon in the sidebar).
2) Select Open Project.
3) Navigate to your existing project folder (the one containing platformio.ini).
4) Click Open.
- PlatformIO will automatically recognize it as a project.
- It will appear in the Explorer and you’ll get build/upload/debug options in the toolbar.

## Install Project Specific Configuration Files
When the project has been successfully installed, platformIO will install the dependent libraries 
listed in ***platformio.ini***. This action generates default configuration files. 
To avoid compile errors and run-time issues, correct configuration files (supplied in the 
***common_files*** folder) need to be copied to the project. Perform the following steps:
1) Copy the file ***conf.h*** from the ***common_files/lvgl*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/lvgl***.
2) Copy the file ***User_Setup.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI***.
3) Copy the file ***User_Setup_Select.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI***.
4) Copy the file ***Setup70b_ESP32_S3_ILI9341.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI/User_Setups***.   
