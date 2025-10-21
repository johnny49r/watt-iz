# Watt-IZ Translator Demo Project
The goal of this project is to demonstrate a functional language translator. This project is
more of a fully functional app as it provides translation of one spoken language to another 
with voice input from the "from" language and text and voice output in the selected "to" 
language. A language pallette of 13 languages is available and many more languages can be
supported.
To run this project you will need the following credentials:
1) A WiFi connection - requires valid SSID and PASSWORD for your router.
2) A valid Google API Key with the following cloud services enabled:
    * Google Cloud Speech To Text API enabled.
    * Google Cloud Text To Speech API enabled.
    * Google Cloud Translation API enabled.

As with other Watt-IZ projects, this project relies on functional Watt-IZ hardware, touch 
LCD, and a small 4 ohm speaker connected to the board. 
Also (as common to other projects) all WiFi credentials and API keys are kept in a JSON
file on the SD card ("wattiz_config.json"). 
See the 'watt_iz_files' project for SD card formatting and generating the JSON config file.

## Project Development Notes
This project is designed to be developed, compiled, and downloaded using the Visual Studio 
Code IDE with the PlatformIO extension installed. The first time this project is installed 
the environment will update all 3rd party libraries listed in platformio.ini under 
"lib_deps = ".
The update may delete/modify configuration files that are important for the project. You can 
find correct files under the /common_files/... folder.
For this project copy lv_conf.h from /common_files/lvgl and paste it into the project folder 
under .pio/libdeps/lvgl.

## Project Operation
After booting swipe to the right to expose the language selection page. Press the 'Translate'
button to begin a six second recording. When the recording has finished the voice recording 
will be translated by Google Speech To Text service. If the text is valid it will be sent to 
the Google Translate service. The output is text (in the "to" language). This text is then 
sent to Google Text To Speech service to provide voice in the desired language.
When the translation is complete, the "from" and "to" text will be displayed on a separate 
page. Scroll up to return to the language selector page.

## Adding Additional Languages
Information about each language is contained in an array called "LanguageArray". Each element in
the array is a structure of type 'language_info_t' whose elements are:
* language_name : Name of the language. Example: "English", "Hindi(India)", etc.
    - Name is arbitrary and appears on the GUI on the language selection GUI rollers.
* lang_code : Google language codes. Example: "en-US", "zh-CN", etc.
    - See https://cloud.google.com/text-to-speech/docs/list-voices-and-types
    - The code determines how the text is interpreted by Google Translate.
* voice_name : Google voice name. Example: "en-US-Wavenet-C", "ja-JP-Wavenet-A", etc.
    - the voice is used by Google Text To Speech to choose voice gender, accent, etc.
* lang_font : The font used to support the treanslated text output. 
    - See next topic 'Creating a New Font'.
* speaking_rate : float value used to modify the rate of speech. 
    - Range from **0.0** (very slow speech rate) to **1.0** (fastest speaking rate). Default = 1.0.

### Creating a New Font
LVGL allows custom fonts created by converting a *TrueType* font to a **.c* file that is compiled 
into the project code.
The conversion is generally done on a computer which has the cli *lv_font_conv* installed. 
See the */common_files/Fonts* folder for info and examples of converting TTF to *.c.
When a new .c file is generated, the font file is placed in the *src/fonts* folder in the 
project directory. 
The code declares a font for use with the statement: *LV_FONT_DECLARE(noto_hindi_18);*
The new font can be referenced in the LanguageArray so the output text will be correct for the
selected language.
**NOTE:** There may be difficuty with languages such as Chinese or Japanese that have a huge number 
of symbols in their written language. Very large font files may not fit in this environments 
with moderate resources avialable.


## Warranty
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. *@Abbycus 2025*

