/*  DC-002 High Precision Digital Clock
HPDL-1414 & Chronodot

pin 0-6   data
Pin 18    Write Module 1
Pin 19    Write Module 2
Pin 20    A0
pin 21    A1
// 62.5 ns per cycle at 16MHz
*/

// EEPROM LAYOUT
// All data is read from lowest bit of relevant byte.
// Byte 0 : Don't Auto Set DST
// Byte 1 : 24 H Time mode
// Byte 2 : Show Seconds
// Byte 3 : Use Celsius

// A note about Time Zones
// The usage of the timezone library here is a bit misleading
// We don't ever actually figure out GMT or consider a real offset, but just keep track
// of time in Standard time, and go +1 hour during daylight savings time.

// Install Teensyduino and set device properly.
#include <TimeLib.h>
#include <Wire.h>
#include <DS3232RTC.h> // From https://github.com/JChristensen/DS3232RTC
#include <Timezone.h> // From https://github.com/JChristensen/Timezone
#include <TimerOne.h>
#include <EEPROM.h>
#include "MenuSystem.h"

// Pin Assignments

#define numberOfModules 2

// For S/N 005 only:
//#define WritePin1 23 //0
//#define WritePin2 11 //1

#define WritePin1 18 //0
#define WritePin2 19 //1
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
#define PinDT 9     // Used for reading DT signal
#define PinSW 12     // Used for the push button switch

#define PinSQW       8 // Square Wave for clock syncing

// Rotary Encoder stuff
volatile unsigned long lastInterruptTime = 0;
int buttonState = HIGH;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 10;

#define display_buf_max_len 32
static char displaybuf[display_buf_max_len];
static int displaybuflen = 0;
static char alternateDisplayBuf[display_buf_max_len];
static int alternateDisplayBufLen = 0;
static bool displayingAlternate = 0;
static bool blinking = 0;

// Menu Timeout (10s)
static long lastMenuInteraction = 0;
#define MENU_TIMEOUT 10000

// Sync sysTime with RTC
// We sync 1ms less than 5 minutes so we won't spend long in the syncing loop
#define SYNC_INTERVAL_MILLIS 299999

//Time Syncing
static time_t timeSyncAtBeginning = 0;
static bool syncInProgress = false;
static time_t lastAttempt = 0;
static unsigned long lastSyncTime = 0;

// Temperature Stuff
unsigned long lastTempReadTime = 0;
int temp = 0;

// Timezones

TimeChangeRule usDaylight = {"DAY", Second, Sun, Mar, 2, 60};  //UTC + 1 Hour = Daylight Local Time
TimeChangeRule usStandard = {"STD", First, Sun, Nov, 2, 0};   //UTC = Standard Local Time
Timezone usTimezones(usDaylight, usStandard);

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

// Current Display
typedef enum {
    DisplayModeClock = 0,
    DisplayModeMenu,
    DisplayModeSetTime,
};
typedef int DisplayMode;

typedef enum {
    ClockModeClock = 0,
    ClockModeDate,
    ClockModeTemp,
    ClockModeCount,
};
typedef int ClockMode;

volatile DisplayMode currentDisplayMode = DisplayModeClock;
volatile ClockMode currentClockMode = ClockModeClock;

// Time Setting

typedef enum {
    SetTimePageDate = 0,
    SetTimePageTime,
    SetTimePageCount,
};
typedef int SetTimePageType;

typedef enum {
    SetTimeField1 = 0,
    SetTimeField2,
    SetTimeField3,
    SetTimeFieldCount,
};
typedef int SetTimeFieldType;

static SetTimePageType setTimePage;
static SetTimeFieldType setTimeField;

static bool setPM;
static int setHr;
static int setMin;
static int setSec;
static int setDay;
static int setMonth;
static int setYr;

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

    // init Preferences
    _autoSetDST = EEPROM.read(0);
    _use24HTime = EEPROM.read(1);
    _showSeconds = EEPROM.read(2);
    _useCelsius =  EEPROM.read(3);
    initMenu();

    setTime(RTC.get());
    
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
    noInterrupts();
    for (int i=0; i < length; i++) {
        writeChar(string[i], i);
    }
    interrupts();
}

void blinkIfNecessary(void) {
    if (displayingAlternate) {
        writeString(displaybuf, numberOfModules * 4);
        displayingAlternate = 0;
    } else {
        writeString(alternateDisplayBuf, numberOfModules * 4);
        displayingAlternate = 1;
    }
}

void inline ensureNotBlinking(void) {
    if (blinking) {
        Timer1.detachInterrupt();
        blinking = 0;
        displayingAlternate = 0;
    }
}

void inline ensureBlinking(void) {
    if (!blinking) {
        blinking = 1;
        displayingAlternate = 0;
        Timer1.attachInterrupt(blinkIfNecessary);
    }
}

void writeNewString(char *inputBuf, int length) {
    if (length > (numberOfModules * 4)) { // limit scrolling.
        length = (numberOfModules * 4);
    }
    if (strncmp(inputBuf, displaybuf, length)) {
        memcpy(displaybuf, inputBuf, length);
        while (length < (numberOfModules * 4)) {
            displaybuf[length] = ' ';
            length++;
        }
        displaybuflen = length;
        ensureNotBlinking();
        writeString(displaybuf, (numberOfModules * 4));
    }
}

void writeNewStrings(char *inputBuf, int length, char *alternateInputBuf, int alternateLength) {
    if (length > (numberOfModules * 4)) { // limit scrolling.
        length = (numberOfModules * 4);
    }
    if (alternateLength > (numberOfModules * 4)) { // limit scrolling.
        alternateLength = (numberOfModules * 4);
    }
    if (strncmp(inputBuf, displaybuf, length) || strncmp(alternateInputBuf, alternateDisplayBuf, alternateLength) ) {
        memcpy(displaybuf, inputBuf, length);
        while (length < (numberOfModules * 4)) {
            displaybuf[length] = ' ';
            length++;
        }
        displaybuflen = length;
        memcpy(alternateDisplayBuf, alternateInputBuf, alternateLength);
        while (alternateLength < (numberOfModules * 4)) {
            displaybuf[alternateLength] = ' ';
            alternateLength++;
        }
        alternateDisplayBufLen = alternateLength;
        writeString(displaybuf, (numberOfModules * 4));
        ensureBlinking();
    }
}

// Menus

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
void on_menu_set_time(MenuItem* selectedItem) {
    hide_menu();
    currentDisplayMode = DisplayModeSetTime;
    setTimePage = SetTimePageDate;
    setTimeField = SetTimeField1;
    // Initialize the time setting procedure!
    time_t localtime = autoSetDST() ? usTimezones.toLocal(now()) : now();
    setPM = isPM(localtime);
    setHr = hourFormat12(localtime);
    setMin = minute(localtime);
    setSec = 0;
    setDay =  day(localtime);
    setMonth = month(localtime);
    setYr = year(localtime) - 2000;
}

void inline hide_menu() {
    currentDisplayMode = DisplayModeClock;
    currentClockMode = ClockModeClock;
    mm.move_to_index(0);
}

void initMenu() {
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

// Time Setting
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

void inline commitNewTimeFromInteractiveSetting() {
        TimeElements newTime;
         // year can be given as full four digit year or two digts (2010 or 10 for 2010);  
         //it is converted to years since 1970
        if (setYr > 99) {
            newTime.Year = (setYr - 1970);
        } else {
            newTime.Year = (setYr + 30);
        }
        newTime.Month = setMonth;
        newTime.Day = constrain(setDay, 1, daysInMonth(setMonth, setYr));
        if (setPM) {
            if (setHr == 12) {
                newTime.Hour = setHr;
            } else {
                newTime.Hour = 12 + setHr;
            }
        } else {
            if (setHr == 12) {
                newTime.Hour = 0;
            } else {
                newTime.Hour = setHr;
            }
        }
        newTime.Minute = setMin;
        newTime.Second = setSec;
        time_t newLocalTime = makeTime(newTime);
        time_t newStdTime = usTimezones.toUTC(newLocalTime);
        RTC.set(newStdTime);
        setTime(newStdTime);
}

void setTimeButtonClick() {
    if (setTimeField == (SetTimeFieldCount - 1)) {
        setTimeField = SetTimeField1;
        setTimePage = setTimePage + 1;
    } else {
        setTimeField = setTimeField + 1;
    }
    if (setTimePage == SetTimePageCount) {
        commitNewTimeFromInteractiveSetting();
        currentDisplayMode = DisplayModeClock;
    }
}

void setTimeEncoderChanged(int delta) {
    int min = 0;
    int max = 99;
    if (setTimePage == SetTimePageTime) {
        if (setTimeField == SetTimeField1) {
            min = 1;
            max = 12;
            setHr = constrain(setHr + delta, min, max);
        } else if (setTimeField == SetTimeField2) {
            min = 0;
            max = 59;
            setMin = constrain(setMin + delta, min, max);
        } if (setTimeField == SetTimeField3) {
            min = 0;
            max = 1;
            setPM = constrain(setPM + delta, min, max);
        }
    } else { // SetTimePageDate
        if (setTimeField == SetTimeField1) {
            min = 1;
            max = 12;
            setMonth = constrain(setMonth + delta, min, max);
        } else if (setTimeField == SetTimeField2) {
            min = 1;
            max = daysInMonth(setMonth, 0);
            setDay = constrain(setDay + delta, min, max);
        } if (setTimeField == SetTimeField3) {
            min = 0;
            max = 99;
            setYr = constrain(setYr + delta, min, max);
        }
    }
}

void inline writeTimeSetUI() {
    char setFormat[12]; // sprintf buffer
    char altSetFormat[12]; // sprintf buffer

    if (setTimePage == SetTimePageTime) {
        if (setTimeField == SetTimeField1) {
            if (setPM) {
                sprintf(setFormat, "%2d:%02d PM", setHr, setMin);
                sprintf(altSetFormat, "  :%02d PM", setMin);
            } else {
                sprintf(setFormat, "%2d:%02d AM", setHr, setMin);
                sprintf(altSetFormat, "  :%02d AM", setMin);
            }
        } else if (setTimeField == SetTimeField2) {
            if (setPM) {
                sprintf(setFormat, "%2d:%02d PM", setHr, setMin);
                sprintf(altSetFormat, "%2d:   PM", setHr);
            } else {
                sprintf(setFormat, "%2d:%02d AM", setHr, setMin);
                sprintf(altSetFormat, "%2d:   AM", setHr);
            }
        } if (setTimeField == SetTimeField3) {
            if (setPM) {
                sprintf(setFormat, "%2d:%02d PM", setHr, setMin);
            } else {
                sprintf(setFormat, "%2d:%02d AM", setHr, setMin);
            }
            sprintf(altSetFormat, "%2d:%02d   ", setHr, setMin);
        }
    } else { // SetTimePageDate
        if (setTimeField == SetTimeField1) {
            sprintf(setFormat, "%2d/%02d/%02d", setMonth, setDay, setYr);
            sprintf(altSetFormat, "  /%02d/%02d", setDay, setYr);
        } else if (setTimeField == SetTimeField2) {
            sprintf(setFormat, "%2d/%02d/%02d", setMonth, setDay, setYr);
            sprintf(altSetFormat, "%2d/  /%02d", setMonth, setYr);
        } if (setTimeField == SetTimeField3) {
            sprintf(setFormat, "%2d/%02d/%02d", setMonth, setDay, setYr);
            sprintf(altSetFormat, "%2d/%02d/  ", setMonth, setDay);
        }
    }

    writeNewStrings(setFormat, strlen(setFormat), altSetFormat, strlen(altSetFormat));
}

bool isLeapYear(int year) {
    return (year % 4) || ((year % 100 == 0) && (year % 400)) ? 0 : 1;
}

// This method returns as if it were a leap year if passed 0
int daysInMonth(int mon, int year) {
    static const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0
    int days = monthDays[mon-1];
    if ((isLeapYear(year) && mon == 2) || year == 0) {
        days = days + 1;
    }
    return days;
}

void inline writeCurrentTime() {
    char timeFormat[12]; // sprintf buffer
    if (timeStatus() == timeSet) {
        time_t localtime = autoSetDST() ? usTimezones.toLocal(now()) : now();
        int hr = use24HTime() ? hour(localtime) : hourFormat12(localtime);
        int min = minute(localtime);
        int sec = second(localtime);
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
    if (timeStatus() == timeSet) {
        time_t localtime = autoSetDST() ? usTimezones.toLocal(now()) : now();
        sprintf(dateFormat, "%2d/%02d/%02d", month(localtime), day(localtime), year(localtime) - 2000);
        writeNewString(dateFormat, strlen(dateFormat));
    }
}

void inline writeCurrentTemperature() {
    char tempFormat[12]; // sprintf buffer
    unsigned long requestTime = millis();
    if (requestTime - lastTempReadTime > 500) {
        temp = RTC.temperature() / 4;
        lastTempReadTime = requestTime;
    }
    if (useCelsius()) {
        sprintf(tempFormat, "  %2d C", temp);
    } else {
        int ftemp = temp * 1.8 + 32;
        sprintf(tempFormat, "  %2d F", ftemp);
    }
    writeNewString(tempFormat, strlen(tempFormat));
}

void inline writeMenu() {
    Menu const* cp_menu = ms.get_current_menu();
    writeNewString(cp_menu->get_selected()->get_name(), strlen(cp_menu->get_selected()->get_name()));
}

void inline readButton() {
    int reading = digitalRead(PinSW);
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != buttonState) {
            buttonState = reading;
            if (buttonState == HIGH) { // On button up, toggle
                processButtonPush();
            }
        }
    }
    lastButtonState = reading;
}

void inline processButtonPush () {
    if ((currentDisplayMode == DisplayModeMenu)) {
        ms.select();
    } else if ((currentDisplayMode == DisplayModeSetTime)) {
        setTimeButtonClick();
    } else {
        currentDisplayMode = DisplayModeMenu;
        lastMenuInteraction = millis();
    } 
}

void inline syncTimeWithRTC() {
    if (syncInProgress) {
        if (lastAttempt != millis()) { //don't query RTC faster than every 1ms.
            time_t currentTime = RTC.get();
            lastAttempt = millis();
            if (currentTime != timeSyncAtBeginning) {
                setTime(currentTime);
                lastSyncTime = millis();
                syncInProgress = false;
            }
        }
    } else {
        if (lastSyncTime <= millis() - SYNC_INTERVAL_MILLIS) {
            timeSyncAtBeginning = RTC.get();
            syncInProgress = true;
        }
    }
}

void loop() {
    syncTimeWithRTC();
    readButton();
    setTimeViaSerialIfAvailable();
    if (currentDisplayMode == DisplayModeMenu) {
        if (millis() - lastMenuInteraction > MENU_TIMEOUT) {
            hide_menu();
        } else {
            writeMenu();
        }
    } else if (currentDisplayMode == DisplayModeSetTime) {
        writeTimeSetUI();
    } else { // DisplayModeClock
        if (currentClockMode == ClockModeTemp) {
            writeCurrentTemperature();
        } else if (currentClockMode == ClockModeDate) {
            writeCurrentDate();
        } else { //currentClockMode == ClockModeClock
            writeCurrentTime();
        }
    }
}

void isr ()  {
    unsigned long interruptTime = millis();
    // If interrupts come faster than 5ms, assume it's a bounce and ignore
    if (interruptTime - lastInterruptTime > 5) {
        if (!digitalRead(PinDT)) {
            if (currentDisplayMode == DisplayModeMenu) {
                lastMenuInteraction = millis();
                ms.next();
            } if (currentDisplayMode == DisplayModeSetTime) {
                setTimeEncoderChanged(1);
            } else {
                currentClockMode = (currentClockMode + 1) % ClockModeCount;
            }
        } else {
            if (currentDisplayMode == DisplayModeMenu) {
                lastMenuInteraction = millis();
                ms.prev();
            } if (currentDisplayMode == DisplayModeSetTime) {
                setTimeEncoderChanged(-1);
            } else {
                if (currentClockMode == ClockModeClock) {
                  currentClockMode = (ClockModeCount -1);
                } else {
                  currentClockMode = (currentClockMode - 1) % ClockModeCount;
                }
            }
        }
    }
    lastInterruptTime = interruptTime;
}
