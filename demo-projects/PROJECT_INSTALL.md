# Instructions for Installing Demo Projects
## Install Visual Studio Code and PlatformIO IDE extensions.
The Watt-IZ demo projects are Arduino style C++ code examples developed using the 
Visual Studio code editor ***(VScode)*** with the popular IDE extension ***PlatformIO***.
Install VScode and the PlatformIO IDE extension using directions here: https://platformio.org/install/ide?install=vscode.

## Copy Project Source (Repository) from Github
### Option Using Git Clone
1) Open a terminal and navigate to a project folder where you want the project to be copied.
2) In the terminal type: *git clone https://github.com/johnny49r/watt-iz.git*.

### Option Downloading Project ZIP File
1) Goto the watt-iz Github project https://github.com/johnny49r/watt-iz/tree/main.
2) Click on the green **<> Code** button and select **Download ZIP**.
3) Extract the ZIP file into your project folder.

## Install Project To VScode.
In VScode, click **File->Open Folder**. Choose the project folder. This gives you visibility 
of all subfolders, but PlatformIO won’t yet treat them as projects.
1) Then click **File->Add Folder to Workspace**.
2) Select **/watt_iz_???/** (the project folder that has its platformio.ini).
3) Repeat for the remaining projects you wish to use.
4) Save this setup: **File->Save Workspace As... watt_iz.code-workspace in the repo root.

## Install A Demo Project in PlatformIO
The Watt-IZ demo projects are organized into individual folders that can be compiled 
and run. Perform the following steps to add a project:
1) In VSCode, go to the PlatformIO Home (alien icon in the sidebar) and choose **Projects**.
2) Select **Open Project**.
3) Navigate to the existing project folder (the one containing platformio.ini).
4) Click Open.
- PlatformIO will automatically recognize it as a project.
- It will appear in the Explorer and you’ll get build/upload/debug options in the toolbar.

## Install Project Specific Configuration Files
When a demo project has been successfully installed, platformIO will install the dependent libraries 
listed in ***platformio.ini***. This action generates default configuration files which will cause 
compile errors and run-time issues. Correct configuration files (supplied in the 
***common_files*** folder) need to be copied to the project. Perform the following steps:

1) Copy the file ***conf.h*** from the ***common_files/lvgl*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/lvgl***.
2) Copy the file ***User_Setup.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI***.
3) Copy the file ***User_Setup_Select.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI***.
4) Copy the file ***Setup70b_ESP32_S3_ILI9341.h*** from the ***common_files/tft_espi*** folder to the project folder
***<project>/.pio/libdeps/esp32-s3-devkitc-1/TFT_eSPI/User_Setups***.

You should now be able to compile, upload, and run Watt-IZ demonstration code.

