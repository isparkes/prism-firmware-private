#ifndef __ESP_GLOBALS_H__
#define __ESP_GLOBALS_H__

#include "HtmlServer.h"
#include "DisplayDefs.h"
#include "ClockDefs.h"
#include "DA2000-Transition.h"
#include "ESP_DS1307.h"
#include "ClockButton.h"

// ----------------------- Components ----------------------------

ClockButton button1(inputPin1, CLOCK_BUTTON_ACTIVE_LO);

Transition transitionWipe(800, 700, 2800, SLOTS_MODE_WIPE_WIPE);  // Wipe In / Wipe Out (DA2000-Transition.h)  
Transition transitionBang(400, 400, 3200, SLOTS_MODE_BANG_BANG);  // Bang In / Bang Out (DA2000-Transition.h)  
Transition transitionDummy(0, 0, 0, SLOTS_MODE_NONE);             // Dummy transition for null pointer prevention
Transition *activeTransition = &transitionDummy;                  // Pointer to selected transition object

OutputManager outputManager;

// ------------- Time management variables -------------

unsigned long nowMillis = 0;
unsigned long lastCheckMillis = 0;
unsigned long lastSecMillis = nowMillis;
unsigned long lastSecond = second();  
boolean secondsChanged = false;

byte currentMode = MODE_TIME;   // Initial cold start mode
byte nextMode = currentMode;
boolean triggeredThisSec = false;

unsigned int tempDisplayModeDuration;      // time for the end of the temporary display
int  tempDisplayMode;

// ------------------- Output buffers ------------------
uint32_t currentDigit1, currentDigit2;
uint32_t prevDigit1, prevDigit2;

// ------------------ Usage statistics -----------------

int impressionsPerSec = 0;
int lastImpressionsPerSec = 0;

// ----------------- Real time clock -------------------

byte useRTC = false;  // true if we detect an RTC
boolean onceHadAnRTC = false;

// ----------------- Motion detector -------------------

unsigned long pirLastSeen = 0;
boolean pirInstalled = false;
int pirConsecutiveCounts = 300;  // set with a high value to stop the PIR being falsely detected the first time round the loop
boolean pirStatus = false;

// --------------------- Blanking ----------------------

boolean blanked = false;
byte blankSuppressStep = 0;    // The press we are on: 1 press = suppress for 1 min, 2 press = 1 hour, 3 = 1 day
unsigned long blankSuppressedMillis = 0;   // The end time of the blanking, 0 if we are not suppressed
unsigned long blankSuppressedSelectionTimoutMillis = 0;   // Used for determining the end of the blanking period selection timeout
boolean blankTubes = false;
boolean blankLEDs = false;

// --------------- Ambient light dimming ---------------

double sensorLDRSmoothed = 0;
double sensorFactor = (double)SENSOR_SENSIT_DEFAULT / 100.0;
int ldrValue = 0;

// ------------------- LED management ------------------

boolean upOrDown;

// Used to control the separator LEDs
boolean led1State;
boolean led2State;
boolean ledLState;
boolean ledRState;

#endif
