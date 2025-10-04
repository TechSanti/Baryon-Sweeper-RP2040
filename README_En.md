Prerequisites
Before you begin, make sure you have the environment set up:

Arduino IDE:

Download and install the latest version of the Arduino IDE (2.x or 1.8.x) from arduino.cc.

Required Libraries:

AES: Install a compatible AES library, such as "AESLib" or "TinyAES," via the Library Manager in the Arduino IDE (Sketch > Include Library > Manage Libraries, search for "AES").
Adafruit NeoPixel: For the RP2040-Zero, install via the Library Manager (search for "Adafruit NeoPixel").

Core RP2040:

Add RP2040 support in the Arduino IDE:

Open the Arduino IDE.
Go to File > Preferences (or Arduino IDE > Preferences on macOS).
In the Additional Boards Manager URLs field, add:
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

Click OK.
Go to Tools > Board > Boards Manager, search for "RP2040," and install the Raspberry Pi Pico/RP2040 by Earle F. Philhower.

Hardware:

Raspberry Pi Pico (regular RP2040) or Waveshare RP2040-Zero.
USB cable to connect the board to the computer.

Step-by-Step Guide to Compiling in .uf2

Open the Project in the Arduino IDE:

Open the Arduino IDE.
Go to File > Open and select the RP2040_v3.2.ino file. The keys.h file will be loaded automatically if it's in the same folder.

Configure the Board:

Go to Tools > Board > Raspberry Pi Pico/RP2040.
Select the specific board:

For Raspberry Pi Pico: Choose Raspberry Pi Pico.
For Waveshare RP2040-Zero: Choose Waveshare RP2040 Zero.

Verify the Code:

Click Sketch > Verify/Compile (shortcut: Ctrl+R) to compile the code.
The IDE will verify the code and report errors if any. Make sure the AES and Adafruit NeoPixel libraries are installed. If there are errors related to the AES library, try another one like "TinyAES" or adjust the code to a compatible library.

Export the .uf2 File:

In the Arduino IDE, go to Sketch > Export compiled Binary (shortcut: Ctrl+Alt+S on Windows/Linux, or equivalent on macOS).
After compiling, the Arduino IDE generates two files in the project folder:
