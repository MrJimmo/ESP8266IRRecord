/*
    ESP8266Record

    This is a fairly simple project that uses IRremoteESP8266 and AdaFruit
    libraries to receive, decode, and display IR Codes.

    Working from the samples of those libraries, I put this together to have a
    device that I could quickly test the output of another more complicated
    IR Sending project (Comcast and VIZIO TV remote project).

    This has also proven to be useful in gather codes from other remotes, mainly
    by running in VSCode and copying the serial output, to generate tables for
    the remote controls I've been working with.

    [Reference / Acknowledgements]
    Most of the IR code below is from/based on the IRRemoteESP8266 examples:
    https://github.com/crankyoldgit/IRremoteESP8266/

    AdaFruit libraries are for drawing graphics/text on the SSD1306 128x64
    display, to show output:
    https://github.com/adafruit/Adafruit_SSD1306
    https://github.com/adafruit/Adafruit-GFX-Library

    Much of the sample code & comments have been pruned and edited for brevity
    or clarity for this implementation.  Refer to the documentation for those
    libraries for better details and info on how they work.

MIT License

Copyright (c) 2025 Jim Moore

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <Arduino.h>
#include <assert.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRac.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Reset pin not used but needed for library
#define OLED_RESET       -1

#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT    64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C

#define TEXT_SIZE_SMALL   1
#define TEXT_SIZE_MEDIUM  2
#define TEXT_SIZE_LARGE   3
#define TEXT_SIZE_XLARGE  4

/*
    HACK:
    The SSD1306 128x64 screen is only 8 lines tall at TEXT_SIZE_SMALL size
    and if more than 2 IR codes are received, additional codes will bleed
    off the bottom of the display (no built-in scrolling capabilities)

    The 'hack' here is to clear the screen each time codes are to be shown, but
    still show all that were returned for one button press.

    Loop() checks IRrecv.decode() each loop, and to prevent a second IR code
    from causing the display to clear, we use the following variables and
    constant to give a little grace time and allow multiple codes per button
    press to be shown.  This is the case for remotes like the Comcast or Xfinity
    XR2 remotes that may send multiple IR codes (Both NEC and XMP).
    
    NOTE: The XFinity XR2 actually emits 3 IR Codes for the "All Power" button.
    No special case in this code to handle that. It just shows up as partial
    output.

    Once the CLEAR_AFTER_MILLISECONDS time has elapsed, the next button press
    (and thus new IR code(s) are received), the display will be cleared and
    results will be shown.  This saves on having to have a reset button.

    Press quick enough (before CLEAR_AFTER_MILLISECONDS elapses), and you can
    see the behavior of multiple codes being lost off the bottom of the screen.

    As a visual, a little 2x2 square is drawn in the lower-right corner of the
    screen after CLEAR_AFTER_MILLISECONDS has passed which means pressing a
    button on the remote will cause the screen to be cleared first.  Pressing
    a button before this square is shown, means new code(s) will likely be drawn
    off the bottom of the screen.

    Yes, quite a hack, but this entire thing is only used to gather and test
    codes for more _important_ projects that emit them :o)
*/
unsigned long g_previousDisplayMillis = 0; // Time button last pressed
unsigned long g_currentMillis         = 0; // Each loop sets with millis();
#define CLEAR_AFTER_MILLISECONDS      2000 // Clear when diff is > than this

// Init the display object with the specs
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, NULL, OLED_RESET);

// IR on GPIO pin 14 (D5 on ESP8266)
const uint16_t kRecvPin = 14;

// The Serial connection baud rate.
const uint32_t kBaudRate = 115200;

// Size of capture buffer.  Tweak if getting mem aloc. error from IRRecv.
const uint16_t kCaptureBufferSize = 4096;

// # of ms of 'no-more-data' before we consider a message ended
const uint8_t kTimeout = 90;

// The smallest sized "UNKNOWN" message packets we actually care about.
const uint16_t kMinUnknownSize = 12;

// kTolerance is defined in IRremoteESP8266\src\IRrecv.h (defaut 25%)
const uint8_t kTolerancePercentage = kTolerance;

// 4th param causes a buffer of kCaptureBufferSize (uint16_t) to be created and
// is used for decoding. Tweak kCaptureBufferSize if getting mem aloc. error.
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);

// Decoded results from IRrecv::decode()
decode_results g_DecodeResults;

/*
displayResults

This function handles displaying the IR results to the SSD1606 display.

In general, it will display...
Protocol (XMP, NEC, etc.)
Code
Address
Commmand

Depending on Protocol, some fields may not be shown.
*/
void displayResults(decode_results ir_results) {

    // Here's the HACK to clear screen if CLEAR_AFTER_MILLISECONDS ellapsed.
    if ((g_currentMillis-g_previousDisplayMillis) > CLEAR_AFTER_MILLISECONDS) {
        display.fillScreen(SSD1306_BLACK); // SSD1306_BLACK
        display.setCursor(0, 0);
    }

    display.setTextSize(TEXT_SIZE_SMALL);
    display.setTextColor(SSD1306_WHITE);

    // Ex. NEC, XMP, SAMSUNG, etc.
    String protocol = String(typeToString(g_DecodeResults.decode_type,
        g_DecodeResults.repeat));
    display.printf("Protocol: %s\n", protocol.c_str());

    // Ex. "Code    : 0x20DF40BF"
    String code = resultToHexidecimal(&g_DecodeResults);
    display.printf("Code    : %s\n", code.c_str());

    // Ex. "Address : 0x04FB (4)"
    if (g_DecodeResults.address > 0) {
        display.printf("Address : 0x%02X%02X (%d)\n",
                        g_DecodeResults.address,
                        (0xFF -g_DecodeResults.address),
                        g_DecodeResults.address);
    }

    // Ex. "Command : 0x02FD (2)"
    if (g_DecodeResults.command > 0) {
        display.printf("Command : 0x%02X%02X (%d)\n",
                        g_DecodeResults.command,
                        (0xFF -g_DecodeResults.command),
                        g_DecodeResults.command);
    }

    display.display();
    g_previousDisplayMillis = millis();
}

/*
setup

Start Serial, set IRRecv object, a display initial "Waiting for IR Code"

*/
void setup() {
    Serial.begin(kBaudRate, SERIAL_8N1, SERIAL_TX_ONLY);

    while (!Serial)  // Wait for the serial connection to be establised.
        delay(50);

    // Perform a low level sanity checks that the compiler performs bit field
    // packing as we expect and Endianness is as we expect.
    assert(irutils::lowLevelSanityCheck() == 0);

    Serial.printf("\n" D_STR_IRRECVDUMP_STARTUP "\n", kRecvPin);
#if DECODE_HASH
    // Ignore messages with less than minimum on or off pulses.
    irrecv.setUnknownThreshold(kMinUnknownSize);
#endif  // DECODE_HASH
    irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.
    irrecv.enableIRIn();  // Start the receiver

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation FAILED"));
        for(;;); // Don't proceed, loop forever
    } else {
        Serial.println(F("SSD1306 allocation SUCCEEDED"));
    }

    display.fillScreen(SSD1306_BLACK); // SSD1306_BLACK
    display.setTextSize(TEXT_SIZE_MEDIUM);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Waiting\nfor\nIR Code..."));
    display.display();
}

/*
loop

Check for new IR codes and display results.

Originally, there were several yield() calls made here, but there's been no
ill-effects with them removed, so leaving them out.  If the device starts
failing the Watch Dog Timer (WDT), then they may need to be added back in.

Example of where they might be used is in the IR Dump examples like:
https://github.com/crankyoldgit/IRremoteESP8266/blob/master/examples/IRrecvDumpV3/IRrecvDumpV3.ino
*/
void loop() {

    g_currentMillis = millis(); // To calculate elapsed time of IR codes recv'd

    // Check if the IR code has been received.
    if (irrecv.decode(&g_DecodeResults) && !g_DecodeResults.repeat) {
        Serial.println("[====== ESP8266IRRecord - BEGIN ======]");

        // Check if we got an IR message that was to big for our capture buffer.
        if (g_DecodeResults.overflow) {
            Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
        }

        // Display the tolerance % if it has been changed from the default.
        if  (kTolerancePercentage != kTolerance) {
            Serial.printf(D_STR_TOLERANCE " : %d%%\n", kTolerancePercentage);
        }

        // Display the basic output of what we found.
        Serial.println("[resultToHumanReadableBasic]:");
        Serial.print(resultToHumanReadableBasic(&g_DecodeResults));

        // Display any extra A/C info if we have it.
        String description = IRAcUtils::resultAcToString(&g_DecodeResults);
        if (description.length()) {
            Serial.println("[resultsAcToString]:");
            Serial.println(D_STR_MESGDESC ": " + description);
        }

        // Output the results as source code
        // From ....IRremoteESP8266\src\IRutils.cpp
        Serial.println("[resultsToSourceCode]:");
        Serial.println(resultToSourceCode(&g_DecodeResults));

        // Ex. "Address: 0x04FB (4)"
        if (g_DecodeResults.address) {
            Serial.printf("Address: 0x%02X%02X (%d)\n",
                g_DecodeResults.address,
                (0xFF - g_DecodeResults.address),
                g_DecodeResults.address);
        }

        // Ex. "Command: 0x02FD (2)"
        if (g_DecodeResults.command) {
            Serial.printf("Command: 0x%02X%02X (%d)\n",
                g_DecodeResults.command,
                (0xFF - g_DecodeResults.command),
                g_DecodeResults.command);
        }

        // Ex. "Value  : 0x0000000020DF40BF"
        if (g_DecodeResults.value) {
            Serial.printf("Value  : 0x%08X%08X\n",
                ( (uint32_t)((g_DecodeResults.value >> 32) & 0xFFFFFFFF) ),
                ( (uint32_t)(g_DecodeResults.value & 0xFFFFFFFF))
            );
        }

        // Call routine to show simple output to SSD1306
        displayResults(g_DecodeResults);

        Serial.println("[====== ESP8266IRRecord - END ======]");
    } else {
        // Draw a little 2x2 white square in bottom-right of screen, indicating
        // it's ok to press button (which will cause screen to clear first)
        if ((g_currentMillis - g_previousDisplayMillis) > 
            CLEAR_AFTER_MILLISECONDS) {
            display.fillRect(
                                (SCREEN_WIDTH  - 2), // X
                                (SCREEN_HEIGHT - 2), // Y
                                2,                   // Width
                                2,                   // Height
                                SSD1306_WHITE        // Color SD1306 (just B/W)
                            );
            display.display();
        }
    }
}
