#include <Wire.h>

/*  HPDL-1414 Chronodot KY-040 Clock for Scott and Mara

pin 0-6   data
Pin 18    Write Module 1
Pin 19    Write Module 2
Pin 20    A0
pin 21    A1
// 62.5 ns per cycle at 16MHz
*/

#include <TimeLib.h>
#include <Wire.h>
#include <DS1307RTC.h>  // a basic DS1307 library that returns time as a time_t
#include <TimerOne.h>
#include <EEPROM.h>

#define LEDPin 11
#define numberOfModules 2

#define WritePin1 18
#define WritePin2 19
#define Address0 20
#define Address1 21

#define DataPin0 0
#define DataPin1 1
#define DataPin2 2
#define DataPin3 3
#define DataPin4 13
#define DataPin5 14
#define DataPin6 15

// RTC stuff
#define RTCSCL 5
#define RTCSDA 6

// Rotary Encoder Stuff
const int PinCLK   = 7;     // Used for generating interrupts using CLK signal
const int PinDT    = 8;     // Used for reading DT signal
const int PinSW    = 11;     // Used for the push button switch

// Rotary Encoder stuff
volatile int virtualPosition = 0;
volatile unsigned long lastInterruptTime = 0;
int lastCount = 128;
const int rotaryEncoderConstrain = 256;

//maybe these three should be volatile.
static char displaybuf[520];
static int displaybuflen = 0;
static int scrolloffset = 0;
static bool scrolling = 0;

// EEPROM LAYOUT
// All data is read from lowest bit of relevant byte.
// Byte 0 : Don't Auto Set DST
// Byte 1 : 24 H Time mode
// Byte 2 : Show Seconds

// Preferences
static int _autoSetDST = 0;
static int _use24HTime = 0;
static int _showSeconds = 0;

int autoSetDST() {
    return _autoSetDST;
}

void setAutoSetDST(int autoSetDST) {
    EEPROM.update(0, autoSetDST);
    _autoSetDST = autoSetDST;
}

int use24HTime() {
    return _use24HTime;
}

void setUse24HTime(int use24HTime) {
    EEPROM.update(1, use24HTime);
    _use24HTime = use24HTime;
}

int showSeconds() {
    return _showSeconds;
}

void setShowSeconds(int showSeconds) {
    EEPROM.update(2, showSeconds);
    _showSeconds = showSeconds;
}

void setup() {
    Serial.begin(57600); // USB is always 12 Mbit/sec
    Serial.setTimeout(0);
    pinMode(LEDPin, OUTPUT);
    pinMode(WritePin1, OUTPUT);
    pinMode(WritePin2, OUTPUT);
    pinMode(Address0, OUTPUT);
    pinMode(Address1, OUTPUT);

    pinMode(DataPin0, OUTPUT);
    pinMode(DataPin1, OUTPUT);
    pinMode(DataPin2, OUTPUT);
    pinMode(DataPin3, OUTPUT);
    pinMode(DataPin4, OUTPUT);
    pinMode(DataPin5, OUTPUT);
    pinMode(DataPin6, OUTPUT);
    
    // Rotary Encoder stuff
    pinMode(PinCLK, INPUT); 
    pinMode(PinDT, INPUT);
    pinMode(PinSW, INPUT_PULLUP);  
    attachInterrupt(PinCLK, isr, FALLING);

    digitalWrite(WritePin1, HIGH);
    digitalWrite(WritePin2, HIGH);
    
    // RTC Stuff
    setSyncProvider(RTC.get);   // the function to get the time from the RTC

    // init Preferences
    _autoSetDST = EEPROM.read(0);
    _use24HTime = EEPROM.read(1);
    _showSeconds = EEPROM.read(2);

    Timer1.initialize(180000);
}

char normalizeChars(char inputCharacter) {
    char result = inputCharacter;
    if(inputCharacter >= 'a' && inputCharacter <= 'z') {
        result = 'A' + inputCharacter - 'a';
    } else if (inputCharacter > 0x5f || inputCharacter < 0x20) {
        // for invalid characters print a space
        result = ' ';
    }
    return result;
}

void writeChar(char character, int index) {
    character = normalizeChars(character);
    int writePin = (index & 0x04) ? WritePin2 : WritePin1;
    digitalWrite(Address0, ~index & 0x01);
    digitalWrite(Address1, ~index & 0x02);
    __asm__("nop\n\t"); // 20ns - tWD
    digitalWrite(writePin, LOW);
    PORTB = character;
    __asm__("nop\n\t"); // 80ns - tDS
    __asm__("nop\n\t"); // 80ns - tDS
    digitalWrite(writePin, HIGH);
    __asm__("nop\n\t"); // 50ns - tDH, tAH
}

void writeString(char string[], int length) {
    for (int i=0; i < length; i++) {
        writeChar(string[i], i);
    }
}

void scrollIfNecessary(void) {
    writeString(displaybuf + scrolloffset, numberOfModules * 4);
    if (scrolloffset >= displaybuflen) {
        scrolloffset = 0;
    } else {
        scrolloffset++;
    }
}

void inline ensureNotScrolling(void) {
    if (scrolling) {
        Timer1.detachInterrupt();
        scrolling = 0;
    }
}

void inline ensureScrolling(void) {
    if (!scrolling) {
        Timer1.attachInterrupt(scrollIfNecessary);
        scrolling = 1;
    }
}

void writeNewString(char *inputBuf, int length) {
    noInterrupts();
    ensureNotScrolling();
    memcpy(displaybuf, inputBuf, length);
    scrolloffset = 0;
    if (length <= (numberOfModules * 4)) {
        while (length < (numberOfModules * 4)) {
            displaybuf[length] = ' ';
            length++;
        }
    }
    displaybuflen = length;
    if (length > (numberOfModules * 4)) {
        displaybuf[length] = ' ';
        memcpy(displaybuf + length + 1, inputBuf, ((numberOfModules * 4) - 1));
        ensureScrolling();
    } else {
        writeString(displaybuf, (numberOfModules * 4));
    }
    interrupts();
}

void inline setTimeViaSerialIfAvailable() {
/*
    static char inputbuf[512];
    static int inputbuflen = 0;

    if (Serial.available()) {
        char newchar = Serial.read();
        inputbuf[inputbuflen] = newchar;
        inputbuflen = (inputbuflen + 1);
        if (newchar == '\n') {
            inputbuflen = inputbuflen - 1; //exclude newline
            writeNewString(inputbuf, inputbuflen);
            saveStringToEEProm(inputbuf, inputbuflen);
            inputbuflen = 0;
        }
    }
*/
}

static int showingMenu = 0;
static int menuLevel = 0;
static int menuPosition = 0;

void inline writeCurrentTime() {
    char timeFormat[12]; // sprintf buffer
    static int lastWrittenH = -1;
    static int lastWrittenM = -1;
    static int lastWrittenS = -1;
    
    if (!showingMenu && timeStatus() == timeSet) {
        int hr = use24HTime() ? hour() : hourFormat12();
        int min = minute();
        int sec = second();
        if (lastWrittenH != hr || lastWrittenM != min || (lastWrittenS != sec && showSeconds())) {
            if (showSeconds()) {
                sprintf(timeFormat, "%2d:%02d:%02d", hr, min, sec);
            } else {
                if (isAM()) {
                    sprintf(timeFormat, "%2d:%02d AM", hr, min);
                } else {
                    sprintf(timeFormat, "%2d:%02d PM", hr, min);
                }
            }
            writeNewString(timeFormat, strlen(timeFormat));
            lastWrittenH = hr;
            lastWrittenM = min;
            lastWrittenS = sec;
        }
    }
}

void inline writeMenu() {
    if (showingMenu) {
        char menuString[12]; // sprintf buffer
        sprintf(menuString, "%d  ", virtualPosition);
        writeNewString(menuString, strlen(menuString));
    }
}

void inline processButtonPush () {

    showingMenu = !showingMenu;
}

void loop() {
    // Handle Input
    if (!(digitalRead(PinSW))) {        // check if pushbutton is pressed
        //virtualPosition = 0;            // if YES, then reset counter to ZERO
        while (!digitalRead(PinSW)) {}  // wait til switch is released
        delay(10);                      // debounce
        processButtonPush();
    }
    
    if (virtualPosition != lastCount) {
        lastCount = virtualPosition;
    }
    
    setTimeViaSerialIfAvailable();
    writeCurrentTime();
    writeMenu();
}


void isr ()  {
    unsigned long interruptTime = millis();
    // If interrupts come faster than 5ms, assume it's a bounce and ignore
    if (interruptTime - lastInterruptTime > 5) {
        int newValue = virtualPosition;
        if (!digitalRead(PinDT)) {
            newValue = (virtualPosition + 1);
        } else {
            newValue = virtualPosition - 1;
        }
        virtualPosition = constrain(newValue, -rotaryEncoderConstrain, rotaryEncoderConstrain);
    }
    lastInterruptTime = interruptTime;
}
