# Watt-IZ 'FILES DEMO' Project

This example project for the Abbycus Watt-IZ Speech enabled ESP32-S3 development board demonstrates 
features of the hardware platform. 

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. @Abbycus 2025

## Directory Display
Folders & files on the SD card will be displayed on the LCD display using the LVGL graphics 
**List** widget. A **DropDown List** widget shows card capacity and card type.

## Speed Test
A simple speed test can be used to benchmark relative performance of the SD card in the ESP32 environment.
Transfer speeds are measured by writing and reading 1024 byte blocks for a total of 1MB transfered. 
Measured times for each block are averaged and displayed after the test has completed.

## SD Card Formatter
Format the SD card with the FAT32 format. ***WARNING*** - all files will be erased!
This can be used on new cards to prepare them for use.

## Write Configuration File to the SD Card
Many of the demo projects require internet access and API keys for Google and OpenAI (Chat GPT) access.
The demos read this information from a file on the SD card named *'wattiz_config.json'*.
When creating this file for use, the information strings are provided by the file 'credentials.h'.

***WARNING:*** The contents of the file *'wattiz_config.json'* are normal text and therefore are not secure.
There are many ways to provide security for your private data but that is outside the scope of this
project.
   
