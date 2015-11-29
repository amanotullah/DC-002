/*  HPDL-1414 test

pin 0-6   data
Pin 18    Write Module 1
Pin 19    Write Module 2
Pin 20    A0
pin 21    A1
// 62.5 ns per cycle at 16MHz
*/

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

//maybe these three should be volatile.
static char displaybuf[520];
static int displaybuflen = 0;
static int scrolloffset = 0;
static bool scrolling = 0;


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

    digitalWrite(WritePin1, HIGH);
    digitalWrite(WritePin2, HIGH);

    char *savedString = readStringFromEEProm();
    writeNewString(savedString, strlen(savedString));

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

static void inline ensureNotScrolling(void) {
    if (scrolling) {
        Timer1.detachInterrupt();
        scrolling = 0;
    }
}

static void inline ensureScrolling(void) {
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

void saveStringToEEProm(char *buffer, int length) {
    byte len = (byte) strlen(buffer);
    EEPROM.write(0, len);
    for (int i = 0; i < len; i++) {
        EEPROM.write(1 + i, buffer[i]);
    }
}

// Returns null terminated string;
char *readStringFromEEProm(void) {
    byte length = EEPROM.read(0);
    static char outputBuf[512];
    for (int i = 0; i < length; i ++) {
        outputBuf[i] = EEPROM.read(i + 1);
    }
    outputBuf[length] = '\0';
    return outputBuf;
}

void loop() {
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
}
