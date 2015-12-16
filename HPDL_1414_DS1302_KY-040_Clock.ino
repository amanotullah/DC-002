#include <Wire.h>

/*  HPDL-1414 Chronodot KY-040 Clock for Scott and Mara

pin 0-6   data
Pin 18    Write Module 1
Pin 19    Write Module 2
Pin 20    A0
pin 21    A1
// 62.5 ns per cycle at 16MHz
*/

/// TODO:
/// UI for setting time
/// DST correction
/// Temperature/Date Display
/// Auto-exit menu after timeout
/// User guide documentation

#include <TimeLib.h>
#include <Wire.h>
#include <DS1307RTC.h>  // a basic DS1307 library that returns time as a time_t
#include <TimerOne.h>
#include <EEPROM.h>
#include "MenuSystem.h"

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
#define PinCLK 7     // Used for generating interrupts using CLK signal
#define PinDT 8     // Used for reading DT signal
#define PinSW 11     // Used for the push button switch

// Rotary Encoder stuff
volatile unsigned long lastInterruptTime = 0;

//maybe these three should be volatile.
#define display_buf_max_len 128
static char displaybuf[display_buf_max_len];
static int displaybuflen = 0;
static int scrolloffset = 0;
static bool scrolling = 0;

// EEPROM LAYOUT
// All data is read from lowest bit of relevant byte.
// Byte 0 : Don't Auto Set DST
// Byte 1 : 24 H Time mode
// Byte 2 : Show Seconds
// Byte 3 : Use Celsius

// Preferences
static char _autoSetDST = 0;
static char _use24HTime = 0;
static char _showSeconds = 0;
static char _useCelsius = 0;

char autoSetDST() {
    return _autoSetDST;
}

void setAutoSetDST(char autoSetDST) {
    EEPROM.update(0, autoSetDST);
    _autoSetDST = autoSetDST;
}

char use24HTime() {
    return _use24HTime;
}

void setUse24HTime(char use24HTime) {
    EEPROM.update(1, use24HTime);
    _use24HTime = use24HTime;
}

char showSeconds() {
    return _showSeconds;
}

void setShowSeconds(char showSeconds) {
    EEPROM.update(2, showSeconds);
    _showSeconds = showSeconds;
}

char useCelsius() {
    return _useCelsius;
}

void setUseCelsius(char useCelsius) {
    EEPROM.update(3, useCelsius);
    _useCelsius = useCelsius;
}

// Menu

MenuSystem ms;  
Menu mm("");  
MenuItem mi_set("Set Time");
Menu mu_auto_dst("Auto DST");
MenuItem mi_auto_dst("Enabled");
MenuItem mi_no_dst("Disabled");
Menu mu_use_24h_time("12/24 Hr");  
MenuItem mi_12h_time("12 Hr");  
MenuItem mi_24h_time("24 Hr");
Menu mu_show_seconds("Show Sec");  
MenuItem mi_show_seconds("Seconds");  
MenuItem mi_hide_seconds("AM/PM");
Menu mu_use_celsius("Temp F/C");  
MenuItem mi_use_fahrenheit("Deg F");
MenuItem mi_use_celsius("Deg C");  
MenuItem mi_exit("Exit");  


void setup() {
    Serial.begin(57600); // USB is always 12 Mbit/sec
    Serial.setTimeout(0);
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
    _useCelsius =  EEPROM.read(3);
    initMenu();
    
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
    if (strncmp(inputBuf, displaybuf, length)) {
        noInterrupts();
        ensureNotScrolling();
        memset(displaybuf, 0, display_buf_max_len);
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
}

#define TIME_HEADER  "T"   // Header tag for serial time sync message
unsigned long processSyncMessage() {
    unsigned long pctime = 0L;
    const unsigned long DEFAULT_TIME = 1420070400; // Jan 1 2015 
    if(Serial.find(TIME_HEADER)) {
        pctime = Serial.parseInt();
        if ( pctime < DEFAULT_TIME) { // check the value is a valid time (greater than Jan 1 2015)
            pctime = 0L; // return 0 to indicate that the time is not valid
        }
    }
    return pctime;
}

void inline setTimeViaSerialIfAvailable() {
    if (Serial.available()) {
        time_t t = processSyncMessage();
        if (t != 0) {
            RTC.set(t);   // set the RTC and the system time to the received value
            setTime(t);          
        }
    }
}

static int showingMenu = 0;
static int displaySelection = 0;
#define displaySelectionCount 3

void on_menu_set_time(MenuItem* selectedItem) {
    hide_menu();
}
void on_menu_auto_dst(MenuItem* selectedItem) {
    setAutoSetDST(1);
    hide_menu();
}
void on_menu_no_dst(MenuItem* selectedItem) {
    setAutoSetDST(0);
    hide_menu();
}
void on_menu_12h_time(MenuItem* selectedItem) {
    setUse24HTime(0);
    hide_menu();
}
void on_menu_24h_time(MenuItem* selectedItem) {
    setUse24HTime(1);
    hide_menu();
}
void on_menu_show_seconds(MenuItem* selectedItem) {
    setShowSeconds(1);
    hide_menu();
}
void on_menu_hide_seconds(MenuItem* selectedItem) {
    setShowSeconds(0);
    hide_menu();
}
void on_menu_use_celsius(MenuItem* selectedItem) {
    setUseCelsius(1);
    hide_menu();
}
void on_menu_use_fahrenheit(MenuItem* selectedItem) {
    setUseCelsius(0);
    hide_menu();
}
void on_menu_exit(MenuItem* selectedItem) {
    hide_menu();
}

void inline hide_menu() {
    showingMenu = 0;
    mm.move_to_index(0);
}

void initMenu() {
    // Menus
    mm.add_item(&mi_set, &on_menu_set_time);
    mm.add_menu(&mu_auto_dst);
    mu_auto_dst.add_item(&mi_auto_dst, &on_menu_auto_dst);
    mu_auto_dst.add_item(&mi_no_dst, &on_menu_no_dst);
    mu_auto_dst.move_to_index(autoSetDST() ? 0 : 1);
    mm.add_menu(&mu_use_24h_time);
    mu_use_24h_time.add_item(&mi_12h_time, &on_menu_12h_time);
    mu_use_24h_time.add_item(&mi_24h_time, &on_menu_24h_time);
    mu_use_24h_time.move_to_index(use24HTime() ? 1 : 0);
    mm.add_menu(&mu_show_seconds);
    mu_show_seconds.add_item(&mi_show_seconds, &on_menu_show_seconds);
    mu_show_seconds.add_item(&mi_hide_seconds, &on_menu_hide_seconds);
    mu_show_seconds.move_to_index(showSeconds() ? 0 : 1);
    mm.add_menu(&mu_use_celsius);
    mu_use_celsius.add_item(&mi_use_fahrenheit, &on_menu_use_fahrenheit);
    mu_use_celsius.add_item(&mi_use_celsius, &on_menu_use_celsius);
    mu_use_celsius.move_to_index(useCelsius() ? 1 : 0);
    mm.add_item(&mi_exit, &on_menu_exit);
    ms.set_root_menu(&mm);
}

void inline writeCurrentTime() {
    char timeFormat[12]; // sprintf buffer
    if (!showingMenu && timeStatus() == timeSet) {
        int hr = use24HTime() ? hour() : hourFormat12();
        int min = minute();
        int sec = second();
        if (showSeconds()) {
            sprintf(timeFormat, "%2d:%02d:%02d", hr, min, sec);
        } else {
            if (use24HTime()) {
                sprintf(timeFormat, " %2d:%02d", hr, min);
            } else {
                if (isAM()) {
                    sprintf(timeFormat, "%2d:%02d AM", hr, min);
                } else {
                    sprintf(timeFormat, "%2d:%02d PM", hr, min);
                }
            }
        }
        writeNewString(timeFormat, strlen(timeFormat));
    }
}

void inline writeCurrentDate() {
    char dateFormat[12]; // sprintf buffer
    if (!showingMenu && timeStatus() == timeSet) {
        sprintf(dateFormat, "%2d/%02d/%02d", month(), day(), year() - 2000);
        writeNewString(dateFormat, strlen(dateFormat));
    }
}

void inline writeCurrentTemperature() {
    char tempFormat[12]; // sprintf buffer
    int temp = 0;
    if (useCelsius()) {
        sprintf(tempFormat, "  %2d C", temp);
    } else {
        sprintf(tempFormat, "  %2d F", temp);
    }
    writeNewString(tempFormat, strlen(tempFormat));
}

void inline writeMenu() {
    if (showingMenu) {
        Menu const* cp_menu = ms.get_current_menu();
        writeNewString(cp_menu->get_selected()->get_name(), strlen(cp_menu->get_selected()->get_name()));
    }
}

void inline processButtonPush () {
    if (showingMenu) {
        ms.select();
    } else {
        showingMenu = !showingMenu;
    } 
}

void loop() {
    // Handle Input
    if (!(digitalRead(PinSW))) {        // check if pushbutton is pressed
        while (!digitalRead(PinSW)) {}  // wait til switch is released
        delay(10);                      // debounce
        processButtonPush();
    }
    
    setTimeViaSerialIfAvailable();
    if (showingMenu) {
        writeMenu();
    } else {
        if (displaySelection == 0) {
            writeCurrentTime();
        } else if (displaySelection == 1) {
            writeCurrentDate();
        } else {
            writeCurrentTemperature();
        }
    }
}


void isr ()  {
    unsigned long interruptTime = millis();
    // If interrupts come faster than 5ms, assume it's a bounce and ignore
    if (interruptTime - lastInterruptTime > 5) {
        if (!digitalRead(PinDT)) {
            if (showingMenu) {
                ms.next();
            } else {
                displaySelection = (displaySelection + 1) % 3;
            }
        } else {
            if (showingMenu) {
                ms.prev();
            } else {
                displaySelection = (displaySelection - 1) % 3;
            }
        }
    }
    lastInterruptTime = interruptTime;
}
