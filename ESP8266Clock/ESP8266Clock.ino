//**********************************************************************************
//* Main code for an ESP8266 based Numitron clock. Features:                       *
//*  - WiFi Clock configuration interface                                          *
//*  - Real Time Clock interface for DS1307+                                       *
//*  - Digit fading with configurable fade length                                  *
//*  - Digit scrollback with configurable scroll speed                             *
//*  - Configuration stored in Flash (JSON)                                        *
//*  - Low hardware component count (as much as possible done in code)             *
//*  - Single button operation with software debounce                              *
//*  - Automatic dimming, using a Light Dependent Resistor                         *
//*  - RGB back light management using individually addressable WS2812B            *
//*  - PIR/motion sensor to turn off display when no one is around                 *
//*                                                                                *
//*   Program with following settings (status line / IDE):                         *
//*                                                                                *
//*    Board: Wemos D1 R2 & Mini / LOLIN (Wemos) D1 R2 & mini                      *
//*    Flash: 80MHz,                                                               *
//*    CPU: 160MHz,                                                                *
//*    Upload speed: 921600,                                                       *
//*    Flash size: 4M (1M SPIFFS)                                                  *
//*    lwIP Variant: v2 Higher Bandwidth (no features)                             *
//*    Erase Flash: Only sketch       ,                                            *
//*                                                                                *
//*  nixie@protonmail.ch                                                           *
//*                                                                                *
//*  board def: http://arduino.esp8266.com/stable/package_esp8266com_index.json    *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************
// Standard Libraries
#include <FS.h>
#include <Wire.h>

// Clock specific libraries, install these with "Sketch -> Include Library -> Add .ZIP library
// using the ZIP files in the "libraries" directory
#include <TimeLib.h>            // http://playground.arduino.cc/code/time (Margolis 1.5.0) // https://github.com/michaelmargolis/arduino_time
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager  (tzapu 0.14.0)
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson (bblanchon 5.13.2)

// Other parts of the code, broken out for clarity
#include "Globals.h"
#include "ClockButton.h"
#include "DA2000-Transition.h"
#include "DebugManager.h"
#include "DisplayDefs.h"
#include "ClockUtils.h"
#include "ClockDefs.h"
#include "ESP_DS1307.h"
#include "HtmlServer.h"
#include "LEDManager.h"
#include "NtpAsync.h"
#include "OutputManagerMicrochip6.h"
#include "SPIFFS.h"

// Feature configuration (append "_OFF" to switch off)
#define FEATURE_FADE
#define FEATURE_SCROLL
#define FEATURE_PREHEAT_OFF
#define FEATURE_STATUS_LEDS_OFF
#define FEATURE_PIR
#define FEATURE_EXT_LEDS_OFF
#define FEATURE_LED_MODES

//**********************************************************************************
//**********************************************************************************
//*                              ISR Display Direct Drive                          *
//**********************************************************************************
//**********************************************************************************
#define INT_MUX_COUNTS         2000
#define COUNTS_PER_DIGIT       20
#define COUNTS_PER_DIGIT_DIM   8

volatile byte phaseStep = 0;
volatile uint32_t outValCurr1, outValCurr2;
volatile uint32_t outValPrev1, outValPrev2;
volatile byte outSwitchTime;
volatile byte outOffTime;
uint32_t bufferValCurr1, bufferValCurr2;
uint32_t bufferValPrev1, bufferValPrev2;
byte bufferSwitchTime;
byte bufferOffTime;

// ************************************************************
// Interrupt routine for scheduled interrupts
//  - performs the programmed action and then schedules the next
//    interrupt for the next display update
//   - Each digit can consist of up to 3 phases:
//     1 - turn on for the "fade from" digit
//     2 - switch to the "fade to" digit
//     3 - turn the digit off
// ************************************************************
ICACHE_RAM_ATTR void displayUpdate() {
  phaseStep++;
  if (phaseStep >= COUNTS_PER_DIGIT) {
    phaseStep = 0;
    outValCurr1 = bufferValCurr1;
    outValCurr2 = bufferValCurr2;
    outValPrev1 = bufferValPrev1;
    outValPrev2 = bufferValPrev2;
    outOffTime = bufferOffTime;
    outSwitchTime = bufferSwitchTime;
    shiftOut32x2(outValCurr1, outValCurr2);
  }

  if (phaseStep == outOffTime) {
    shiftOut32x2(0, 0);
  }
  
//  if (phaseStep == outSwitchTime) {
//    shiftOut32x2(outValPrev1, outValPrev2);
//  }
  
  timer1_write(INT_MUX_COUNTS);
}

// ************************************************************
// Perform the parallel shift out to the registers
// ************************************************************
ICACHE_RAM_ATTR void shiftOut32x2(uint32_t _val1, uint32_t _val2) {
  uint8_t i;

  for (i = 0; i < 32; i++) {
    digitalWrite(DATAPin, !!(_val2 & (1 << (31 - i))));
    digitalWrite(CLOCKPin, HIGH);
    digitalWrite(CLOCKPin, LOW);
  }
  for (i = 0; i < 32; i++) {
    digitalWrite(DATAPin, !!(_val1 & (1 << (31 - i))));
    digitalWrite(CLOCKPin, HIGH);
    digitalWrite(CLOCKPin, LOW);
  }

  // Latch in
  digitalWrite(LATCHPin, HIGH);
  digitalWrite(LATCHPin, LOW);
}

//**********************************************************************************
//**********************************************************************************
//*                                    Setup                                       *
//**********************************************************************************
//**********************************************************************************
void setup()
{
  // Set up the output manager
  OutputManager::CreateInstance();
  OutputManager::Instance().setUp();
  OutputManager::Instance().setLDRValue(0);
  OutputManager::Instance().allNormal(false);
  OutputManager::Instance().setConfigObject(&current_config);
  
  // Show Start message on tubes
  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_START);
  OutputManager::Instance().outputDisplay();

  WiFiManager wifiManager;

  // set up the NTP component and wire up the "got time update" callback
  ntpAsync.setUp();
  NewTimeCallback ntcb = newTimeUpdateReceived;
  ntpAsync.setNewTimeCallback(ntcb);

  bool debugVal = true;
  debugManager.setUp(debugVal);
  wifiManager.setDebugOutput(debugVal);

  DebugCallback dbcb = debugMsgLocal;
  ntpAsync.setDebugCallback(dbcb);
  ntpAsync.setDebugOutput(debugVal);

  spiffs.setDebugCallback(dbcb);
  spiffs.setDebugOutput(debugVal);

  ledManager.setUp();

  // ----------------------------------------------------------------------------

  debugMsg("Starting display interrupt handler");
  
  timer1_attachInterrupt(displayUpdate); // Add ISR Function
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  /* Dividers:
    TIM_DIV1 = 0,   //80MHz (80 ticks/us - 104857.588 us max)
    TIM_DIV16 = 1,  //5MHz (5 ticks/us - 1677721.4 us max)
    TIM_DIV256 = 3  //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
  Reloads:
    TIM_SINGLE  0 //on interrupt routine you need to write a new value to start the timer again
    TIM_LOOP  1 //on interrupt the counter will start with the same value again
  */

  timer1_write(INT_MUX_COUNTS * COUNTS_PER_DIGIT);

  // ----------------------------------------------------------------------------

  debugMsg("Digit test");
  uint32_t test = 1;
  bufferValCurr1 = 0;
  bufferValCurr2 = 0;

  // No switching or dimming
  bufferSwitchTime = 21;
  bufferOffTime = 21;
  
  while (test != 0) {
    bufferValCurr2 = test;
    // debugManager.debugMsg("Set test to: " + String(test));
    test = test << 1;
    delay(100);
  }
  test = 1;
  while (test != 0) {
    bufferValCurr1 = test;
    // debugManager.debugMsg("Set test to: " + String(test));
    test = test << 1;
    delay(100);
  }
  
  setDiagnosticLED(DIAGS_START, STATUS_GREEN);
  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_START);
  OutputManager::Instance().outputDisplay();

  // Start I2C now so that the update from NTP can be sent to the RTC immediately
  Wire.begin(4, 5); // SDA = D2 = pin 4, SCL = D1 = pin 5
  debugManager.debugMsg("I2C master started");

  button1.reset();

  setDiagnosticLED(DIAGS_SPIFFS, STATUS_YELLOW);
  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_SPIFFS);
  OutputManager::Instance().outputDisplay();

  for (int i = 0; i < 25 ; i++) button1.checkButton(millis());

  if (button1.isButtonPressedNow()) {
    setDiagnosticLED(DIAGS_SPIFFS, STATUS_BLUE);
    factoryReset();
    delay(1000);
    ESP.restart();
  }

  if (!spiffs.getConfigFromSpiffs(&current_config)) {
    if (spiffs.testMountSpiffs()) {
      debugManager.debugMsg("No config found - resetting everything");
      factoryReset();
      setDiagnosticLED(DIAGS_SPIFFS, STATUS_BLUE);
    } else {
      // we couldn't mount SPIFFS because no SPIFFS is available
      setDiagnosticLED(DIAGS_SPIFFS, STATUS_RED);
    }
  } else {
    setDiagnosticLED(DIAGS_SPIFFS, STATUS_GREEN);

    setWebAuthentication(current_config.webAuthentication);
    setWebUserName(current_config.webUsername);
    setWebPassword(current_config.webPassword);
    
    ntpAsync.setTZS(current_config.tzs);
    ntpAsync.setNtpPool(current_config.ntpPool);
    ntpAsync.setUpdateInterval(current_config.ntpUpdateInterval);

    ledManager.recalculateVariables();
  }

  // **********************************************************************

  // Clear down any spurious button action
  button1.reset();

  if (current_config.testMode) {
    debugManager.debugMsg("Doing test sequence");
    delay(1000);
    boolean oldUseLDR = current_config.useLDR;
    byte oldbacklightMode = current_config.backlightMode;

    // turn off LDR
    current_config.useLDR = false;

    // set backlights to change with the displayed digits
    current_config.backlightMode = BACKLIGHT_COLOUR_TIME;

    // All the digits on
    OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);

    lastCheckMillis = millis();

    boolean inLoop = true;
    int secCount = 0;

    while (inLoop) {
      // Otherwise we get SOFT WDT
      yield();

      nowMillis = millis();
      if (nowMillis - lastCheckMillis > 1000) {
        lastCheckMillis = nowMillis;
        secCount++;
        secCount = secCount % 10;
      }

      OutputManager::Instance().loadNumberArraySameValue(secCount);

      if (secCount % 2 == 0) {
        if (secCount == 8) {
          ledLState = processStatusMode(STATUS_LED_MODE_ON);
          ledRState = processStatusMode(STATUS_LED_MODE_ON);
          led1State = true;
          led2State = true;
        } else {
          ledLState = processStatusMode(STATUS_LED_MODE_OFF);
          ledRState = processStatusMode(STATUS_LED_MODE_OFF);
          led1State = true;
          led2State = false;
        }
      } else {
        ledLState = processStatusMode(STATUS_LED_MODE_ON);
        ledRState = processStatusMode(STATUS_LED_MODE_ON);
        led1State = false;
        led2State = true;
      }

      OutputManager::Instance().outputDisplay();

      setLeds();

      // debugManager.debugMsg("Checing test mode exit condition");
      button1.checkButton(nowMillis);
      if (button1.isButtonPressedNow() && (secCount == 8)) {
        inLoop = false;
        current_config.testMode = false;
        spiffs.saveConfigToSpiffs(&current_config);

        // reset the LEDs after the test pattern
        setDiagnosticLED(DIAGS_START, STATUS_GREEN);
        setDiagnosticLED(DIAGS_SPIFFS, STATUS_GREEN);
      }
    }

    current_config.useLDR = oldUseLDR;
    current_config.backlightMode = oldbacklightMode;
  }

  button1.reset();

  // **********************************************************************

  wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
  wifiManager.setAPCallback(configModeCallback);

  setDiagnosticLED(DIAGS_WIFI, STATUS_YELLOW);
  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_WIFI);
  OutputManager::Instance().outputDisplay();

  boolean connected = false;
  connected = wifiManager.autoConnect(WiFi.hostname().c_str(), "SetMeUp!");

  if (connected) {
    debugManager.debugMsg("Connected!");
    setDiagnosticLED(DIAGS_WIFI, STATUS_GREEN);
  } else {
    debugManager.debugMsg("Could NOT Connect");
    setDiagnosticLED(DIAGS_WIFI, STATUS_RED);
  }

  IPAddress apIP = WiFi.softAPIP();
  IPAddress myIP = WiFi.localIP();
  debugManager.debugMsg("AP IP address: " + formatIPAsString(apIP));
  debugManager.debugMsg("IP address: " + formatIPAsString(myIP));

  // If we lose the connection, we try to recover it
  WiFi.setAutoReconnect(true);

  // See if we can already get the time
  ntpAsync.getTimeFromNTP();
  setDiagnosticLED(DIAGS_NTP, STATUS_YELLOW);
  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_NTP);
  OutputManager::Instance().outputDisplay();

  long startSeachMs = millis();
  bool needUpdate = true;
  while (((millis() - startSeachMs) < 10000) && needUpdate) {
    delay(1000);
    ntpAsync.getTimeFromNTP();
    if (ntpAsync.ntpTimeValid(millis()) > 0) {
      debugManager.debugMsg("Got update from NTP, exiting loop");
      needUpdate = false;
    }
  }

  if (needUpdate) {
    setDiagnosticLED(DIAGS_NTP, STATUS_RED);
  } else {
    setDiagnosticLED(DIAGS_NTP, STATUS_GREEN);
  }

  setServerPageHandlers();
  server.begin();
  debugManager.debugMsg("HTTP server started");

  // Enable OTA updates
  httpUpdater.setup(&server, "/update", "admin", "update");

  if (mdns.begin(WiFi.hostname(), WiFi.localIP())) {
    debugManager.debugMsg("MDNS responder started as http://" + WiFi.hostname() + ".local");
    mdns.addService("http", "tcp", 80);
  }

  setPIRPullup(current_config.usePIRPullup);

  // initialise the internal time (in case we don't find the time provider)
  nowMillis = millis();

  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_RTC);
  OutputManager::Instance().outputDisplay();
  testRTCTimeProvider();
  if (useRTC) {
    getRTCTime(true);
    setDiagnosticLED(DIAGS_RTC, STATUS_GREEN);
  } else {
    setDiagnosticLED(DIAGS_RTC, STATUS_RED);
  }

  // Note that we had an RTC to allow the indicator DP to have meaning
  onceHadAnRTC = useRTC;
  
  // Set the time and set to  show the version
  OutputManager::Instance().loadNumberArrayTime();
  tempDisplayMode = TEMP_MODE_VERSION;
  tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;

  // don't blank anything right now
  blanked = false;
  setTubesAndLEDSblankMode();

  OutputManager::Instance().loadNumberArrayPOSTMessage(DIAGS_DEBUG);
  OutputManager::Instance().outputDisplay();
  
  if (debugManager.getDebug()) {
    setDiagnosticLED(DIAGS_DEBUG, STATUS_BLUE);
  } else {
    setDiagnosticLED(DIAGS_DEBUG, STATUS_GREEN);
  }

  debugManager.debugMsg("Exit startup");
  spiffs.getStatsFromSpiffs(&current_stats);
  ledManager.setDayOfWeek(weekday());
}

//**********************************************************************************
//**********************************************************************************
//*                              Main loop                                         *
//**********************************************************************************
//**********************************************************************************
void loop()
{
  server.handleClient();
  mdns.update();

  nowMillis = millis();

  // shows us how fast the inner loop is running
  impressionsPerSec++;

  // -------------------------------------------------------------------------------

  if (lastSecond != second()) {
    lastSecond = second();
    lastSecMillis = nowMillis;
    secondsChanged = true;
    performOncePerSecondProcessing();

    if ((second() == 0) && (!triggeredThisSec)) {
      if ((minute() == 0)) {
        if (hour() == 0) {
          performOncePerDayProcessing();
        }
        performOncePerHourProcessing();
      }
      performOncePerMinuteProcessing();
    }

    // Make sure we don't call multiple times
    triggeredThisSec = true;

    if ((second() > 0) && triggeredThisSec) {
      triggeredThisSec = false;
    }
  }

  // Check button, we evaluate below
  button1.checkButton(nowMillis);

  // ******* Preview the next display mode *******
  // What is previewed here will get actioned when
  // the button is released
  if (button1.isButtonPressed2S()) {
    // Just jump back to the start
    nextMode = MODE_MIN;
  } else if (button1.isButtonPressed1S()) {
    nextMode = currentMode + 1;

    if (nextMode > MODE_MAX) {
      nextMode = MODE_MIN;
    }
  }

  // ******* Set the display mode *******
  if (button1.isButtonPressedReleased2S()) {
    currentMode = MODE_MIN;

    // Store the latest config if we exit the config mode
    spiffs.saveConfigToSpiffs(&current_config);

    // Preset the display
    OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);

    nextMode = currentMode;
  } else if (button1.isButtonPressedReleased1S()) {
    currentMode++;

    if (currentMode > MODE_MAX) {
      currentMode = MODE_MIN;

      // Store the latest config if we exit the config mode
      spiffs.saveConfigToSpiffs(&current_config);

      // Preset the display
      OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);
    }

    nextMode = currentMode;
  }

  // ************* Process the modes *************
  if (nextMode != currentMode) {
    setNextMode(nextMode);
  } else {
    processCurrentMode(currentMode);
  }

  if (digitalRead(pirPin) == HIGH) {
    pirConsecutiveCounts++;
  }

  ldrValue = getDimmingFromLDR();
  ledManager.setLDRValue(ldrValue);
  OutputManager::Instance().setLDRValue(ldrValue);
  OutputManager::Instance().outputDisplay();

  setLeds();

  delay(10);
}

// ************************************************************
// Called once per second
// ************************************************************
void performOncePerSecondProcessing() {
  // Store the current value and reset
  lastImpressionsPerSec = impressionsPerSec;
  impressionsPerSec = 0;

  // If we are in temp display mode, decrement the count
  if (tempDisplayModeDuration > 0) {
    if (tempDisplayModeDuration > 1000) {
      tempDisplayModeDuration -= 1000;
    } else {
      tempDisplayModeDuration = 0;
    }
  }

  // decrement the blanking supression counter
  if (blankSuppressedMillis > 0) {
    if (blankSuppressedMillis > 1000) {
      blankSuppressedMillis -= 1000;
    } else {
      blankSuppressedMillis = 0;
    }
  }

  // Get the blanking status, this may be overridden by blanking suppression
  // Only blank if we are in TIME mode
  if (currentMode == MODE_TIME) {
    boolean pirBlanked = checkPIR(nowMillis);
    boolean nativeBlanked = checkBlanking();

    // Check if we are in blanking suppression mode
    blanked = (nativeBlanked || pirBlanked) && (blankSuppressedMillis == 0);

    if (blanked) {
      debugManager.debugMsg("blanked");
      if (pirBlanked)
        debugManager.debugMsg(" -> PIR");
      if (nativeBlanked)
        debugManager.debugMsg(" -> Native");
    }

    // reset the blanking period selection timer
    if (nowMillis > blankSuppressedSelectionTimoutMillis) {
      blankSuppressedSelectionTimoutMillis = 0;
      blankSuppressStep = 0;
    }
  } else {
    blanked = false;
  }

  setTubesAndLEDSblankMode();

  // Decrement the value display counter
  // 0xff means "forever", so don't decrement it
  if ((OutputManager::Instance().getValueDisplayTime() > 0) && (OutputManager::Instance().getValueDisplayTime() < 0xff)) {
    OutputManager::Instance().decValueDisplayTime();
  }

  // Reset PIR debounce
  pirConsecutiveCounts = 0;

  if (ntpAsync.getNextUpdate(nowMillis) < 0) {
    ntpAsync.getTimeFromNTP();
  }

  // Set the indicator states
  ledLState = processStatusMode(current_config.statusModeL);
  ledRState = processStatusMode(current_config.statusModeR);
}

// ************************************************************
// Called once per minute
// ************************************************************
void performOncePerMinuteProcessing() {
  debugManager.debugMsg("---> OncePerMinuteProcessing");

  debugManager.debugMsg("nu: " + String(ntpAsync.getNextUpdate(nowMillis)));

  // Set the internal time to the time from the RTC even if we are still in
  // NTP valid time. This is more accurate than using the internal time source
  getRTCTime(true);

  // Usage stats
  current_stats.uptimeMins++;

  if (!blankTubes) {
    current_stats.tubeOnTimeMins++;
  }

  // RTC - light up only if we have lost our RTC
  if (onceHadAnRTC && !useRTC) {
    // debugManager.debugMsg("lost access to RTC");
  }

  if (getIsConnected()) {
    IPAddress ip = WiFi.localIP();
    debugManager.debugMsg("IP: " + formatIPAsString(ip));
  } else {
    IPAddress ip = WiFi.localIP();
    debugManager.debugMsg("Not connected IP: " + formatIPAsString(ip));
  }
}

// ************************************************************
// Called once per hour
// ************************************************************
void performOncePerHourProcessing() {
  debugManager.debugMsg("---> OncePerHourProcessing");
  reconnectDroppedConnection();
}

// ************************************************************
// Called once per day
// ************************************************************
void performOncePerDayProcessing() {
  debugManager.debugMsg("---> OncePerDayProcessing");
  spiffs.saveStatsToSpiffs(&current_stats);
  ledManager.setDayOfWeek(weekday());
}

// ************************************************************
// Set the seconds tick led(s) and the back lights
// ************************************************************
void setLeds()
{
  if (secondsChanged) {
    lastCheckMillis = nowMillis;
    upOrDown = !upOrDown;
    secondsChanged = false;
  }

  unsigned int secsDelta;
  int secsDeltaAbs = (nowMillis - lastCheckMillis);
  
  if (upOrDown) {
    secsDelta = (nowMillis - lastCheckMillis);
  } else {
    secsDelta = 1000 - (nowMillis - lastCheckMillis);
  }

  // --------------------------------------- separators --------------------------------------
  
  switch (current_config.ledMode) {
    case LED_RAILROAD:
      {
        if (upOrDown) {
          led1State = true;
          led2State = false;
        } else {
          led1State = false;
          led2State = true;
        }
        break;
      }
    case LED_RAILROAD_X:
      {
        if (upOrDown) {
          led1State = true;
          led2State = true;
        } else {
          led1State = false;
          led2State = false;
        }
        break;
      }
    case LED_BLINK_SLOW:
      {
        if (upOrDown) {
          led1State = true;
          led2State = true;
        } else {
          led1State = false;
          led2State = false;
        }
        break;
      }
    case LED_BLINK_FAST:
      {
        if (secsDeltaAbs < 500) {
          led1State = true;
          led2State = true;
        } else {
          led1State = false;
          led2State = false;
        }
        break;
      }
    case LED_BLINK_DBL:
      {
        if ((secsDeltaAbs < 100) || ((secsDeltaAbs > 200) && (secsDeltaAbs < 300))) {
          led1State = true;
          led2State = true;
        } else {
          led1State = false;
          led2State = false;
        }
        break;
      }
    case LED_ON:
      {
        led1State = true;
        led2State = true;
        break;
      }
    case LED_OFF:
      {
        led1State = false;
        led2State = false;
        break;
      }
  }

  // output the backlight/underlight LEDs
  ledManager.setPulseValue(secsDelta);  
  ledManager.processLedStatus();
}

// ************************************************************
// Check the PIR status. If we don't have a PIR installed, we
// don't want to respect the pin value, because it would defeat
// normal day blanking. The first time the PIR takes the pin low
// we mark that we have a PIR and we should start to respect
// the sensor over configured blanking.
// Returns true if PIR sensor is installed and we are blanked
//
// To deal with noise, we use a counter to detect a number of
// consecutive counts before counting the PIR as "detected"
// ************************************************************
boolean checkPIR(unsigned long nowMillis) {
  if (pirConsecutiveCounts > (lastImpressionsPerSec / 2)) {
    pirLastSeen = nowMillis;
    return false;
  } else {
    if (!pirInstalled) debugManager.debugMsg("Marking PIR as installed");
    pirInstalled = true;
    if (nowMillis > (pirLastSeen + (current_config.pirTimeout * 1000))) {
      pirStatus = false;
      return true;
    } else {
      pirStatus = true;
      return false;
    }
  }
}

// ************************************************************
// Check if the PIR sensor is installed
// ************************************************************
boolean checkPIRInstalled() {
  return pirInstalled;
}

// ************************************************************
// Show the preview of the next mode - we stay in this mode until the
// button is released
// ************************************************************
void setNextMode(int displayMode) {
  // turn off blanking
  blanked = false;
  setTubesAndLEDSblankMode();

  switch (displayMode) {
    case MODE_TIME: {
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
        break;
      }
    case MODE_HOURS_SET: {
        if (ntpAsync.ntpTimeValid(nowMillis)) {
          // skip past the time and date settings
          setNewNextMode(MODE_12_24);
        }
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight0and1();
        break;
      }
    case MODE_MINS_SET: {
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight2and3();
        break;
      }
    case MODE_SECS_SET: {
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight4and5();
        break;
      }
    case MODE_DAYS_SET: {
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightDaysDateFormat();
        break;
      }
    case MODE_MONTHS_SET: {
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightMonthsDateFormat();
        break;
      }
    case MODE_YEARS_SET: {
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightYearsDateFormat();
        break;
      }
    case MODE_12_24: {
        OutputManager::Instance().loadNumberArrayConfBool(current_config.hourMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_LEAD_BLANK: {
        OutputManager::Instance().loadNumberArrayConfBool(current_config.blankLeading, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_DATE_FORMAT: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.dateFormat, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_DAY_BLANKING: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.dayBlanking, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_HR_BLNK_START: {
        if ((current_config.dayBlanking == DAY_BLANKING_NEVER) ||
            (current_config.dayBlanking == DAY_BLANKING_WEEKEND) ||
            (current_config.dayBlanking == DAY_BLANKING_WEEKDAY) ||
            (current_config.dayBlanking == DAY_BLANKING_ALWAYS)) {
          // Skip past the start and end hour if the blanking mode says it is not relevant
          setNewNextMode(MODE_USE_LDR);
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankHourStart, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_HR_BLNK_END: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankHourEnd, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_USE_LDR: {
        OutputManager::Instance().loadNumberArrayConfBool(current_config.useLDR, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BLANK_MODE: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_SLOTS_MODE: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.slotsMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_LED_BLINK: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.ledMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_PIR_TIMEOUT_UP:
    case MODE_PIR_TIMEOUT_DOWN: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.pirTimeout, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BACKLIGHT_MODE: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.backlightMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_RED_CNL: {
        if ((current_config.backlightMode == BACKLIGHT_CYCLE) || (current_config.backlightMode == BACKLIGHT_COLOUR_TIME) || (current_config.backlightMode == BACKLIGHT_DAY_OF_WEEK)) {
          setNewNextMode(MODE_CYCLE_SPEED);
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.redCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_GRN_CNL: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.grnCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BLU_CNL: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.bluCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_CYCLE_SPEED: {
        if (current_config.backlightMode != BACKLIGHT_CYCLE) {
          // Show only if we are in cycle mode
          setNewNextMode(MODE_MIN_DIM_UP);
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.cycleSpeed, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_MIN_DIM_UP:
    case MODE_MIN_DIM_DOWN: {
        OutputManager::Instance().loadNumberArrayConfInt(current_config.minDim, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_USE_PIR_PULLUP: {
        OutputManager::Instance().loadNumberArrayConfBool(current_config.usePIRPullup, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_VERSION: {
        OutputManager::Instance().loadNumberArrayConfInt(SOFTWARE_VERSION, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_TUBE_TEST: {
        OutputManager::Instance().loadNumberArrayTestDigits();
        OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);
        break;
      }
  }
}

// ************************************************************
// Show the next mode - once the button is released
// ************************************************************
void processCurrentMode(int displayMode) {
  static boolean msgDisplaying = false;

  switch (displayMode) {
    case MODE_TIME: {
        if (button1.isButtonPressedAndReleased()) {
          // Deal with blanking first
          if ((nowMillis < blankSuppressedSelectionTimoutMillis) || blanked) {
            if (blankSuppressedSelectionTimoutMillis == 0) {
              // Apply 5 sec tineout for setting the suppression time
              blankSuppressedSelectionTimoutMillis = nowMillis + TEMP_DISPLAY_MODE_DUR_MS;
            }

            blankSuppressStep++;
            if (blankSuppressStep > 3) {
              blankSuppressStep = 3;
            }

            if (blankSuppressStep == 1) {
              blankSuppressedMillis = 10000;
            } else if (blankSuppressStep == 2) {
              blankSuppressedMillis = 3600000;
            } else if (blankSuppressStep == 3) {
              blankSuppressedMillis = 3600000 * 4;
            }
            blanked = false;
            setTubesAndLEDSblankMode();
          } else {
            // Always start from the first mode, or increment the temp mode if we are already in a display
            if (tempDisplayModeDuration > 0) {
              tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;
              tempDisplayMode++;
            } else {
              tempDisplayMode = TEMP_MODE_MIN;
            }

            if (tempDisplayMode > TEMP_MODE_MAX) {
              tempDisplayMode = TEMP_MODE_MIN;
            }

            tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;
          }
        }

        if (tempDisplayModeDuration > 0) {
          blanked = false;
          setTubesAndLEDSblankMode();
          if (tempDisplayMode == TEMP_MODE_DATE) {
            OutputManager::Instance().loadNumberArrayDate();
          }

          if (tempDisplayMode == TEMP_MODE_LDR) {
            OutputManager::Instance().loadNumberArrayLDR();
          }

          if (tempDisplayMode == TEMP_MODE_VERSION) {
            OutputManager::Instance().loadNumberArrayConfInt(SOFTWARE_VERSION, 0);
          }

          if (tempDisplayMode == TEMP_IP_ADDR12) {
            if (getIsConnected()) {
              IPAddress myIP = WiFi.localIP();
              OutputManager::Instance().loadNumberArrayIP(myIP[0], myIP[1]);
            } else {
              // we can't show the IP address if we have the RTC, just skip
              tempDisplayMode++;
            }
          }

          if (tempDisplayMode == TEMP_IP_ADDR34) {
            if (getIsConnected()) {
              IPAddress myIP = WiFi.localIP();
              OutputManager::Instance().loadNumberArrayIP(myIP[2], myIP[3]);
            } else {
              // we can't show the IP address if we have the RTC, just skip
              tempDisplayMode++;
            }
          }

          if (tempDisplayMode == TEMP_ESP_ID) {
            if (getIsConnected()) {
              String shortHostName = WiFi.hostname().substring(4, 10);
              shortHostName.toLowerCase();
              //debugManager.debugMsg("ESPID: " + shortHostName);
              OutputManager::Instance().loadNumberArrayESPID(shortHostName);
            } else {
              // we can't show the IP address if we have the RTC, just skip
              tempDisplayMode++;
            }
          }

          if (tempDisplayMode == TEMP_IMPR) {
            OutputManager::Instance().loadNumberArrayConfInt(lastImpressionsPerSec, 0);
          }

          OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);

        } else {
          if (current_config.slotsMode > SLOTS_MODE_MIN) {

            // Which slots transition are we using?
            if (!msgDisplaying) {
              switch (current_config.slotsMode) {
                case SLOTS_MODE_WIPE_WIPE: {
                    activeTransition = &transitionWipe;
                    break;
                  }
                case SLOTS_MODE_BANG_BANG: {
                    activeTransition = &transitionBang;
                    break;
                  }
                default: {
                    activeTransition = &transitionDummy; // Insurance against null pointers
                  }
              }
            }

            // Initialise the slots transition values and start it
            if (second() == 50 && !msgDisplaying) {
              activeTransition->start(nowMillis);
            }

            // Continue slots transition
            msgDisplaying = activeTransition->runEffect(nowMillis, current_config.blankLeading);
            if (msgDisplaying) {
              activeTransition->updateRegularDisplaySeconds(second());
            }
            else {
              // no slots mode, check if we are in valueDisplayMode
            if (OutputManager::Instance().getValueDisplayTime() > 0) {
                OutputManager::Instance().loadNumberArrayValueToShow();
                OutputManager::Instance().loadDisplaySetValueType();
              } else {
                // Do normal time thing when we are not in slots
                OutputManager::Instance().loadNumberArrayTime();
                OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
              }
            }
          }
          else {
            // no slots mode, check if we are in valueDisplayMode
            if (OutputManager::Instance().getValueDisplayTime() > 0) {
              OutputManager::Instance().loadNumberArrayValueToShow();
              OutputManager::Instance().loadDisplaySetValueType();
            } else {
              // no slots mode, just do normal time thing
              OutputManager::Instance().loadNumberArrayTime();
              OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
            }
          }
        }
        break;
      }
    case MODE_MINS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incMins();
        }
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight2and3();
        break;
      }
    case MODE_HOURS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incHours();
        }
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight0and1();
        break;
      }
    case MODE_SECS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          resetSecond();
        }
        OutputManager::Instance().loadNumberArrayTime();
        OutputManager::Instance().highlight4and5();
        break;
      }
    case MODE_DAYS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incDays();
        }
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightDaysDateFormat();
        break;
      }
    case MODE_MONTHS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incMonths();
        }
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightMonthsDateFormat();
        break;
      }
    case MODE_YEARS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incYears();
        }
        OutputManager::Instance().loadNumberArrayDate();
        OutputManager::Instance().highlightYearsDateFormat();
        break;
      }
    case MODE_12_24: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.hourMode = ! current_config.hourMode;
        }
        OutputManager::Instance().loadNumberArrayConfBool(current_config.hourMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_LEAD_BLANK: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.blankLeading = !current_config.blankLeading;
        }
        OutputManager::Instance().loadNumberArrayConfBool(current_config.blankLeading, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_DATE_FORMAT: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.dateFormat++;
          if (current_config.dateFormat > DATE_FORMAT_MAX) {
            current_config.dateFormat = DATE_FORMAT_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.dateFormat, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_DAY_BLANKING: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.dayBlanking++;
          if (current_config.dayBlanking > DAY_BLANKING_MAX) {
            current_config.dayBlanking = DAY_BLANKING_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.dayBlanking, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_HR_BLNK_START: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.blankHourStart++;
          if (current_config.blankHourStart > HOURS_MAX) {
            current_config.blankHourStart = 0;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankHourStart, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_HR_BLNK_END: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.blankHourEnd++;
          if (current_config.blankHourEnd > HOURS_MAX) {
            current_config.blankHourEnd = 0;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankHourEnd, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BLANK_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.blankMode++;
          if (current_config.blankMode > BLANK_MODE_MAX) {
            current_config.blankMode = BLANK_MODE_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.blankMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_USE_LDR: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.useLDR = !current_config.useLDR;
        }
        OutputManager::Instance().loadNumberArrayConfBool(current_config.useLDR, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_SLOTS_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.slotsMode++;
          if (current_config.slotsMode > SLOTS_MODE_MAX) {
            current_config.slotsMode = SLOTS_MODE_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.slotsMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_LED_BLINK: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.ledMode++;
          if (current_config.ledMode > LED_MODE_MAX) {
            current_config.ledMode = LED_MODE_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.ledMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_PIR_TIMEOUT_UP: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.pirTimeout += 10;
          if (current_config.pirTimeout > PIR_TIMEOUT_MAX) {
            current_config.pirTimeout = PIR_TIMEOUT_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.pirTimeout, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_PIR_TIMEOUT_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.pirTimeout -= 10;
          if (current_config.pirTimeout < PIR_TIMEOUT_MIN) {
            current_config.pirTimeout = PIR_TIMEOUT_MAX;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.pirTimeout, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BACKLIGHT_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.backlightMode++;
          if (current_config.backlightMode > BACKLIGHT_MAX) {
            current_config.backlightMode = BACKLIGHT_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.backlightMode, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_RED_CNL: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.redCnl++;
          if (current_config.redCnl > COLOUR_CNL_MAX) {
            current_config.redCnl = COLOUR_CNL_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.redCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_GRN_CNL: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.grnCnl++;
          if (current_config.grnCnl > COLOUR_CNL_MAX) {
            current_config.grnCnl = COLOUR_CNL_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.grnCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_BLU_CNL: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.bluCnl++;
          if (current_config.bluCnl > COLOUR_CNL_MAX) {
            current_config.bluCnl = COLOUR_CNL_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.bluCnl, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_CYCLE_SPEED: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.cycleSpeed = current_config.cycleSpeed + 2;
          if (current_config.cycleSpeed > CYCLE_SPEED_MAX) {
            current_config.cycleSpeed = CYCLE_SPEED_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.cycleSpeed, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_MIN_DIM_UP: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.minDim += 10;
          if (current_config.minDim > MIN_DIM_MAX) {
            current_config.minDim = MIN_DIM_MAX;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.minDim, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_MIN_DIM_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.minDim -= 10;
          if (current_config.minDim < MIN_DIM_MIN) {
            current_config.minDim = MIN_DIM_MIN;
          }
        }
        OutputManager::Instance().loadNumberArrayConfInt(current_config.minDim, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_USE_PIR_PULLUP: {
        if (button1.isButtonPressedAndReleased()) {
          current_config.usePIRPullup = !current_config.usePIRPullup;
        }
        OutputManager::Instance().loadNumberArrayConfBool(current_config.usePIRPullup, displayMode);
        digitalWrite(pirPin, current_config.usePIRPullup);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_VERSION: {
        OutputManager::Instance().loadNumberArrayConfInt(SOFTWARE_VERSION, displayMode);
        OutputManager::Instance().displayConfig();
        break;
      }
    case MODE_TUBE_TEST: {
        OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);
        OutputManager::Instance().loadNumberArrayTestDigits();
        break;
      }
  }
}

//**********************************************************************************
//**********************************************************************************
//*                             Utility functions                                  *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Reset the seconds to 00
// ************************************************************
void resetSecond() {
  byte tmpSecs = 0;
  setTime(hour(), minute(), tmpSecs, day(), month(), year());
  setRTCTime();
}

// ************************************************************
// increment the time by 1 Sec
// ************************************************************
void incSecond() {
  byte tmpSecs = second();
  tmpSecs++;
  if (tmpSecs >= SECS_MAX) {
    tmpSecs = 0;
  }
  setTime(hour(), minute(), tmpSecs, day(), month(), year());
  setRTCTime();
}

// ************************************************************
// increment the time by 1 min
// ************************************************************
void incMins() {
  byte tmpMins = minute();
  tmpMins++;
  if (tmpMins >= MINS_MAX) {
    tmpMins = 0;
  }
  setTime(hour(), tmpMins, 0, day(), month(), year());
  setRTCTime();
}

// ************************************************************
// increment the time by 1 hour
// ************************************************************
void incHours() {
  byte tmpHours = hour();
  tmpHours++;

  if (tmpHours >= HOURS_MAX) {
    tmpHours = 0;
  }
  setTime(tmpHours, minute(), second(), day(), month(), year());
  setRTCTime();
}

// ************************************************************
// increment the date by 1 day
// ************************************************************
void incDays() {
  byte tmpDays = day();
  tmpDays++;

  int maxDays;
  switch (month())
  {
    case 4:
    case 6:
    case 9:
    case 11:
      {
        maxDays = 31;
        break;
      }
    case 2:
      {
        // we won't worry about leap years!!!
        maxDays = 28;
        break;
      }
    default:
      {
        maxDays = 31;
      }
  }

  if (tmpDays > maxDays) {
    tmpDays = 1;
  }
  setTime(hour(), minute(), second(), tmpDays, month(), year());
  setRTCTime();
}

// ************************************************************
// increment the month by 1 month
// ************************************************************
void incMonths() {
  byte tmpMonths = month();
  tmpMonths++;

  if (tmpMonths > 12) {
    tmpMonths = 1;
  }
  setTime(hour(), minute(), second(), day(), tmpMonths, year());
  setRTCTime();
}

// ************************************************************
// increment the year by 1 year
// ************************************************************
void incYears() {
  byte tmpYears = year() % 100;
  tmpYears++;

  if (tmpYears > 50) {
    tmpYears = 15;
  }
  setTime(hour(), minute(), second(), day(), month(), 2000 + tmpYears);
  setRTCTime();
}

// ************************************************************
// Set the time from the value we get back from the timer server
// ************************************************************
void setTimeFromServer(String timeString) {
  int intValues[6];
  grabInts(timeString, &intValues[0], ",");
  setTime(intValues[SYNC_HOURS], intValues[SYNC_MINS], intValues[SYNC_SECS], intValues[SYNC_DAY], intValues[SYNC_MONTH], intValues[SYNC_YEAR]);
  debugManager.debugMsg("Set internal time to NTP time: " + String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second()));
}

// ************************************************************
// Set the Status LED
// ************************************************************
bool processStatusMode(byte mode) {
#ifdef FEATURE_STATUS_LEDS
  switch (mode) {
    case STATUS_LED_MODE_NTP: {
        return ntpAsync.ntpTimeValid(nowMillis);
        break;
      }
    case STATUS_LED_MODE_ONLINE: {
        return getIsConnected();
        break;
      }
    case STATUS_LED_MODE_AM_PM: {
        return isPM();
        break;
      }
    case STATUS_LED_MODE_PIR: {
        return pirStatus;
        break;
      }
    case STATUS_LED_MODE_OFF: {
        return false;
        break;
      }
    case STATUS_LED_MODE_ON: {
        return true;
        break;
      }
  }
#endif
}

// ************************************************************
// Check the blanking, overridden if we have a PIR installed
// ************************************************************
boolean checkBlanking() {
  // If we are running on PIR, never use native blanking
  if (checkPIRInstalled())
    return false;

  // Check day blanking, but only when we are in
  // normal time mode
  if (currentMode == MODE_TIME) {
    switch (current_config.dayBlanking) {
      case DAY_BLANKING_NEVER:
        return false;
      case DAY_BLANKING_HOURS:
        return getHoursBlanked();
      case DAY_BLANKING_WEEKEND:
        return ((weekday() == 1) || (weekday() == 7));
      case DAY_BLANKING_WEEKEND_OR_HOURS:
        return ((weekday() == 1) || (weekday() == 7)) || getHoursBlanked();
      case DAY_BLANKING_WEEKEND_AND_HOURS:
        return ((weekday() == 1) || (weekday() == 7)) && getHoursBlanked();
      case DAY_BLANKING_WEEKDAY:
        return ((weekday() > 1) && (weekday() < 7));
      case DAY_BLANKING_WEEKDAY_OR_HOURS:
        return ((weekday() > 1) && (weekday() < 7)) || getHoursBlanked();
      case DAY_BLANKING_WEEKDAY_AND_HOURS:
        return ((weekday() > 1) && (weekday() < 7)) && getHoursBlanked();
      case DAY_BLANKING_ALWAYS:
        return true;
    }
  }
}

// ************************************************************
// Get the readable version of the PIR state
// ************************************************************
String getPIRStateDisplay() {
  float lastMotionDetection = (nowMillis - pirLastSeen) / 1000.0;

  String part3 = " ago";
  if (blanked) {
    part3 = " ago (blanked)";
  }
  return "Motion detected " + secsToReadableString(lastMotionDetection) + part3;
}

// ************************************************************
// If we are currently blanked based on hours
// ************************************************************
boolean getHoursBlanked() {
  if (current_config.blankHourStart > current_config.blankHourEnd) {
    // blanking before midnight
    return ((hour() >= current_config.blankHourStart) || (hour() < current_config.blankHourEnd));
  } else if (current_config.blankHourStart < current_config.blankHourEnd) {
    // dim at or after midnight
    return ((hour() >= current_config.blankHourStart) && (hour() < current_config.blankHourEnd));
  } else {
    // no dimming if Start = End
    return false;
  }
}

// ************************************************************
// Set the tubes and LEDs blanking variables based on blanking mode and
// blank mode settings
// ************************************************************
void setTubesAndLEDSblankMode() {
  if (blanked) {
    switch (current_config.blankMode) {
      case BLANK_MODE_TUBES:
        {
          blankTubes = true;
          blankLEDs = false;
          break;
        }
      case BLANK_MODE_LEDS:
        {
          blankTubes = false;
          blankLEDs = true;
          break;
        }
      case BLANK_MODE_BOTH:
        {
          blankTubes = true;
          blankLEDs = true;
          break;
        }
    }
  } else {
    blankTubes = false;
    blankLEDs = false;
  }

  ledManager.setBlanked(blankLEDs);
}

// ************************************************************
// Set the state of the internal PIR pullup resistor
// ************************************************************
void setPIRPullup(boolean newState) {
  if (newState) {
    debugManager.debugMsg("Setting PIR pullup");
    pinMode(pirPin, INPUT_PULLUP);
  } else {
    debugManager.debugMsg("Resetting PIR pullup");
    pinMode(pirPin, INPUT);
  }
}

// ************************************************************
// Jump to a new position in the menu - used to skip unused items
// ************************************************************
void setNewNextMode(int newNextMode) {
  nextMode = newNextMode;
  currentMode = newNextMode - 1;
}

// ************************************************************
// Callback: When the NTP component tells us there is an update
// go and get it
// ************************************************************
void newTimeUpdateReceived() {
  debugManager.debugMsg("Got a new time update");
  setTimeFromServer(ntpAsync.getLastTimeFromServer());
  setRTCTime();
}

// ************************************************************
// Shim to ledManager function - adds a delay
// ************************************************************
void setDiagnosticLED(byte stepNumber, byte state) {
  ledManager.setDiagnosticLED(stepNumber, state);
  delay(500);
}

//**********************************************************************************
//**********************************************************************************
//*                         RTC Module Time Provider                               *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Get the time from the RTC
// ************************************************************
String getRTCTime(boolean setInternalTime) {
  testRTCTimeProvider();
  if (useRTC) {
    Clock.getTime();
    int years = Clock.year + 2000;
    byte months = Clock.month;
    byte days = Clock.dayOfMonth;
    byte hours = Clock.hour;
    byte mins = Clock.minute;
    byte secs = Clock.second;

    String returnValue = String(years) + ":" + String(months) + ":" + String(days) + " " + String(hours) + ":" + String(mins) + ":" + String(secs);
    debugManager.debugMsg("Got RTC time: " + returnValue);

    if (setInternalTime) {
      // Set the internal time provider to the value we got
      setTime(hours, mins, secs, days, months, years);
      debugManager.debugMsg("Set Internal time to: " + returnValue);
    }

    return returnValue;
  }
}

// ************************************************************
// Check that we still have access to the time from the RTC
// ************************************************************
void testRTCTimeProvider() {
  // Set up the time provider
  // first try to find the RTC, if not available, go into slave mode
  Wire.beginTransmission(RTC_I2C_ADDRESS);
  useRTC = (Wire.endTransmission() == 0);
}

// ************************************************************
// Set the date/time in the RTC from the internal time
// Always hold the time in 24 format, we convert to 12 in the
// display.
// ************************************************************
void setRTCTime() {
  testRTCTimeProvider();
  if (useRTC) {
    Clock.fillByYMD(year() % 100, month(), day());
    Clock.fillByHMS(hour(), minute(), second());
    Clock.setTime();

    debugManager.debugMsg("Set RTC time to internal time: " + String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second()));
  }
}

//**********************************************************************************
//**********************************************************************************
//*                          Light Dependent Resistor                              *
//**********************************************************************************
//**********************************************************************************

// ******************************************************************
// Check the ambient light through the LDR (Light Dependent Resistor)
// Smooths the reading over a number of reads.
//
// The LDR in bright light gives reading of around 50, the reading in
// total darkness is around 900.
//
// The return value is the dimming count we are using. 999 is full
// brightness, 100 is very dim.
//
// Because the floating point calculation may return more than the
// maximum value, we have to clamp it as the final step
// ******************************************************************
int getDimmingFromLDR() {
  if (current_config.useLDR) {
    int rawLDR = analogRead(LDRPin);
    int rawSensorVal = 1023 - rawLDR;

    double sensorDiff = rawSensorVal - sensorLDRSmoothed;
    sensorLDRSmoothed += (sensorDiff / (double)current_config.sensorSmoothCountLDR);

    // Scaling offset increases the base brightness
    // factor increases the sensitivity
    double offset = current_config.thresholdBright;
    double factor = current_config.sensitivityLDR / 5.0;

    int returnValue = (sensorLDRSmoothed + offset) / factor;

    if (returnValue < current_config.minDim) returnValue = current_config.minDim;
    if (returnValue > COUNTS_PER_DIGIT) returnValue = COUNTS_PER_DIGIT;
    return returnValue;
  } else {
    return COUNTS_PER_DIGIT;
  }
}

// ************************************************************
// Reset configuration values back to what they once were
// ************************************************************
void factoryReset() {
  ntpAsync.resetDefaults();
  spiffs_config_t* cc = &current_config;
  cc->ntpPool = ntpAsync.getNtpPool();
  cc->ntpUpdateInterval = ntpAsync.getUpdateInterval();
  cc->tzs = ntpAsync.getTZS();

  cc->hourMode = HOUR_MODE_DEFAULT;
  cc->blankLeading = LEAD_BLANK_DEFAULT;
  cc->dateFormat = DATE_FORMAT_DEFAULT;
  cc->dayBlanking = DAY_BLANKING_DEFAULT;

  cc->thresholdBright = SENSOR_THRSH_DEFAULT;
  cc->sensorSmoothCountLDR = SENSOR_SMOOTH_READINGS_DEFAULT;
  cc->sensitivityLDR = SENSOR_SENSIT_DEFAULT;
  cc->minDim = MIN_DIM_DEFAULT;

  cc->dateFormat = DATE_FORMAT_DEFAULT;
  cc->dayBlanking = DAY_BLANKING_DEFAULT;
  cc->backlightMode = BACKLIGHT_DEFAULT;
  cc->useBLDim = true;
  cc->useBLPulse = false;
  cc->redCnl = COLOUR_RED_CNL_DEFAULT;
  cc->grnCnl = COLOUR_GRN_CNL_DEFAULT;
  cc->bluCnl = COLOUR_BLU_CNL_DEFAULT;
  cc->blankHourStart = 0;
  cc->blankHourEnd = 7;
  cc->cycleSpeed = CYCLE_SPEED_DEFAULT;
  cc->pirTimeout = PIR_TIMEOUT_DEFAULT;
  cc->useLDR = USE_LDR_DEFAULT;
  cc->blankMode = BLANK_MODE_DEFAULT;
  cc->slotsMode = SLOTS_MODE_DEFAULT;
  cc->usePIRPullup = USE_PIR_PULLUP_DEFAULT;
  cc->ledMode = LED_BLINK_DEFAULT;
  cc->testMode = true;
  setWebAuthentication(WEB_AUTH_DEFAULT);
  cc->webAuthentication = getWebAuthentication();
  setWebUserName(WEB_USERNAME_DEFAULT);
  cc->webUsername = getWebUserName();
  setWebPassword(WEB_PASSWORD_DEFAULT);
  cc->webPassword = getWebPassword();
  cc->backlightDimFactor = BACKLIGHT_DIM_FACTOR_DEFAULT;
  cc->statusModeL = STATUS_LED_MODE_ONLINE;
  cc->statusModeR = STATUS_LED_MODE_NTP;
  cc->preheatStrength = 0;
  cc->extDimFactor = EXT_DIM_FACTOR_DEFAULT;
  cc->separatorDimFactor = SEP_DIM_DEFAULT;
  cc->fadeSteps = FADE_STEPS_DEFAULT;
  cc->scrollSteps = SCROLL_STEPS_DEFAULT;

  spiffs.saveConfigToSpiffs(cc);
}

// ************************************************************
// Conditionally trigger the save (don't write if we don't have
// any changes - that just wears out the flash)
// ************************************************************
void saveToSpiffsIfChanged(boolean changed) {
  if (changed) {
    debugManager.debugMsg("Config options changed, saving them");
    spiffs.saveConfigToSpiffs(&current_config);

    // Save the stats while we are at it
    spiffs.saveStatsToSpiffs(&current_stats);
  }
}

//**********************************************************************************
//**********************************************************************************
//*                                Page Handlers                                   *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Summary page
// ************************************************************
void rootPageHandler() {
  debugManager.debugMsg("Root page in");

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  // Status table
  response_message += getTableHead2Col("Current Status", "Name", "Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    response_message += getTableRow2Col("WLAN IP", formatIPAsString(ip));
    response_message += getTableRow2Col("WLAN MAC", WiFi.macAddress());
    response_message += getTableRow2Col("WLAN SSID", WiFi.SSID());
    response_message += getTableRow2Col("NTP Pool", ntpAsync.getNtpPool());
    response_message += getTableRow2Col("TZ", ntpAsync.getTZS());
    response_message += getTableRow2Col("Last NTP time", ntpAsync.getLastTimeFromServer());
    String clockUrl = "http://" + WiFi.hostname() + ".local";
    clockUrl.toLowerCase();
    response_message += getTableRow2Col("Clock Name", WiFi.hostname() + " (<a href = \"" + clockUrl + "\">" + clockUrl + "</a>)");
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);
    response_message += getTableRow2Col("AP IP", ipStrAP);
    response_message += getTableRow2Col("AP MAC", WiFi.softAPmacAddress());
  }

  response_message += getTableRow2Col("Uptime", secsToReadableString(millis() / 1000));

  long lastUpdateTimeSecs = ntpAsync.getLastUpdateTimeSecs(millis());
  response_message += getTableRow2Col("Last time update", secsToReadableString(lastUpdateTimeSecs) + " ago");

  signed long absNextUpdate = abs(ntpAsync.getNextUpdate(millis()));
  String overdueInd = "";
  if (absNextUpdate < 0) {
    overdueInd = " overdue";
    absNextUpdate = -absNextUpdate;
  }
  response_message += getTableRow2Col("Time before next update", secsToReadableString(absNextUpdate) + overdueInd);

  response_message += getTableRow2Col("Version", SOFTWARE_VERSION);

  // Scan I2C bus
  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      String slaveMsg = "Found I2C slave at";
      response_message += getTableRow2Col(slaveMsg, idx);
    }
  }

  response_message += getTableFoot();

  // ******************** Clock Info table ***************************
  float digitBrightness = getDimmingFromLDR() * 100.0 / (float) COUNTS_PER_DIGIT;
  String motionSensorState = checkPIRInstalled() ? getPIRStateDisplay() : "Not installed";
  String rtcState = useRTC ? "Installed" : "Not installed";
  String timeSource;

  if (ntpAsync.ntpTimeValid(nowMillis)) {
    timeSource = "NTP";
  } else if (useRTC) {
    timeSource = "RTC";
  } else {
    timeSource = "Internal";
  }
  String currentTime = String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second());
  response_message += getTableHead2Col("Clock information", "Name", "Value");

  if (current_config.useLDR) {
    response_message += getTableRow2Col("LDR Value", sensorLDRSmoothed);
  } else {
    response_message += getTableRow2Col("LDR Value", "LDR disabled");
  }

  response_message += getTableRow2Col("Digit brightness %", String(digitBrightness, 2));
  response_message += getTableRow2Col("Motion Sensor", motionSensorState);
  response_message += getTableRow2Col("Time Source", timeSource);
  response_message += getTableRow2Col("Display Time", currentTime);
  response_message += getTableRow2Col("Real Time Clock", rtcState);
  if (useRTC) {
    response_message += getTableRow2Col("RTC Time", getRTCTime(false));
  }
  response_message += getTableRow2Col("Impressions/Sec", lastImpressionsPerSec);
  response_message += getTableRow2Col("Total Clock On Hrs", secsToReadableString(current_stats.uptimeMins * 60));
  response_message += getTableRow2Col("Total Tube On Hrs", secsToReadableString(current_stats.tubeOnTimeMins * 60));
  response_message += getTableFoot();

  // ******************** ESP8266 Info table ***************************
  response_message += getTableHead2Col("ESP8266 information", "Name", "Value");
  response_message += getTableRow2Col("Sketch size", ESP.getSketchSize());
  response_message += getTableRow2Col("Free sketch size", ESP.getFreeSketchSpace());
  response_message += getTableRow2Col("Sketch hash", ESP.getSketchMD5());
  response_message += getTableRow2Col("Free heap", ESP.getFreeHeap());
  response_message += getTableRow2Col("Boot version", ESP.getBootVersion());
  response_message += getTableRow2Col("CPU Freqency (MHz)", ESP.getCpuFreqMHz());
  response_message += getTableRow2Col("Flash speed (MHz)", ESP.getFlashChipSpeed() / 1000000);
  response_message += getTableRow2Col("SDK version", ESP.getSdkVersion());
  response_message += getTableRow2Col("Chip ID", String(ESP.getChipId(), HEX));
  response_message += getTableRow2Col("Flash Chip ID", String(ESP.getFlashChipId(), HEX));
  response_message += getTableRow2Col("Flash size", ESP.getFlashChipRealSize());
  response_message += getTableFoot();

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Root page out");
}

// ************************************************************
// Get the local time from the time server, and modify the time server URL if needed
// ************************************************************
void timeServerPageHandler() {
  debugManager.debugMsg("Time page in");

  boolean changed = false;

  checkServerArgString("ntppool", "NTP Pool", changed, current_config.ntpPool);
  checkServerArgInt("ntpupdate", "NTP update interval", changed, current_config.ntpUpdateInterval);
  checkServerArgString("tzs", "Time Zone String", changed, current_config.tzs);
  if (changed) {
    ntpAsync.setTZS(current_config.tzs);
    ntpAsync.setNtpPool(current_config.ntpPool);
    ntpAsync.setUpdateInterval(current_config.ntpUpdateInterval);
  }
  saveToSpiffsIfChanged(changed);

  // -----------------------------------------------------------------------------

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Select time server");

  debugManager.debugMsg("ntpPool is: " + ntpAsync.getNtpPool());
  response_message += getTextInputWide("NTP Pool", "ntppool", ntpAsync.getNtpPool(), false);
  response_message += getNumberInput("Update interval:", "ntpupdate", NTP_UPDATE_INTERVAL_MIN, NTP_UPDATE_INTERVAL_MAX, ntpAsync.getUpdateInterval(), false);
  response_message += getTextInputWide("Time Zone String", "tzs", ntpAsync.getTZS(), false);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Time page out");
}

// ************************************************************
// Reset the EEPROM and WiFi values, restart
// ************************************************************
void resetPageHandler() {
  debugManager.debugMsg("Reset page in");

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Reset done.</div></div>";

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Reset page out");

  // let the message get written
  delay(500);

  // do the restart
  ESP.restart();
}

// ************************************************************
// Page for the clock configuration
// ************************************************************
void clockConfigPageHandler() {
  debugManager.debugMsg("Config page in");

  boolean changed = false;

  // -----------------------------------------------------------------------------
  checkServerArgBoolean("12h24hMode", "12/24h mode", "12h", "24h", changed, current_config.hourMode);
  checkServerArgBoolean("blankLeading", "Leading zero", "blank", "show", changed, current_config.blankLeading);
  checkServerArgByte("dateFormat", "dateFormat", changed, current_config.dateFormat);
  checkServerArgByte("slotsMode", "slotsMode", changed, current_config.slotsMode);
#ifdef FEATURE_PREHEAT
  checkServerArgByte("preheatStrength", "preheatStrength", changed, current_config.preheatStrength);
#endif
#ifdef FEATURE_SCROLL
  checkServerArgBoolean("scrollback", "Use scrollback", "on", "off", changed, current_config.scrollback);
  checkServerArgByte("scrollSteps", "scrollSteps", changed, current_config.scrollSteps);
#endif
#ifdef FEATURE_FADE
  checkServerArgBoolean("fade", "Use fade", "on", "off", changed, current_config.fade);
  checkServerArgByte("fadeSteps", "fadeSteps", changed, current_config.fadeSteps);
#endif
  // -----------------------------------------------------------------------------
  checkServerArgBoolean("useLDR", "Use LDR", "on", "off", changed, current_config.useLDR);
  checkServerArgInt("minDim", "minDim", changed, current_config.minDim);
  checkServerArgInt("thresholdBright", "thresholdBright", changed, current_config.thresholdBright);
  checkServerArgInt("sensitivityLDR", "sensitivityLDR", changed, current_config.sensitivityLDR);
  // -----------------------------------------------------------------------------
#ifdef FEATURE_PIR
  checkServerArgInt("pirTimeout", "pirTimeout", changed, current_config.pirTimeout);
  checkServerArgBoolean("usePIRPullup", "Use PIR pullup", "on", "off", changed, current_config.usePIRPullup);
#endif
  checkServerArgByte("dayBlanking", "dayBlanking", changed, current_config.dayBlanking);
  checkServerArgByte("blankMode", "blankMode", changed, current_config.blankMode);
  checkServerArgByte("blankHourStart", "blankHourStart", changed, current_config.blankHourStart);
  checkServerArgByte("blankHourEnd", "blankHourEnd", changed, current_config.blankHourEnd);
  // -----------------------------------------------------------------------------
  checkServerArgByte("backlightMode", "backlightMode", changed, current_config.backlightMode);
  checkServerArgByte("cycleSpeed", "cycleSpeed", changed, current_config.cycleSpeed);
  checkServerArgBoolean("useBLPulse", "Use BL pulse", "on", "off", changed, current_config.useBLPulse);
  checkServerArgBoolean("useBLDim", "Use BL dim", "on", "off", changed, current_config.useBLDim);
  // -----------------------------------------------------------------------------
  checkServerArgByte("redCnl", "redCnl", changed, current_config.redCnl);
  checkServerArgByte("grnCnl", "grnCnl", changed, current_config.grnCnl);
  checkServerArgByte("bluCnl", "bluCnl", changed, current_config.bluCnl);
  checkServerArgByte("backlightDimFactor", "backlightDimFactor", changed, current_config.backlightDimFactor);
#ifdef FEATURE_EXT_LEDS
  checkServerArgByte("extDimFactor", "extDimFactor", changed, current_config.extDimFactor);
#endif
  
  // -----------------------------------------------------------------------------
#ifdef FEATURE_LED_MODES
  checkServerArgByte("ledMode", "ledMode", changed, current_config.ledMode);
#endif

#ifdef FEATURE_STATUS_LEDS
  checkServerArgByte("statusModeL", "statusModeL", changed, current_config.statusModeL);
  checkServerArgByte("statusModeR", "statusModeR", changed, current_config.statusModeR);
#endif
  checkServerArgByte("separatorDimFactor", "separatorDimFactor", changed, current_config.separatorDimFactor);
  // -----------------------------------------------------------------------------
  boolean webConfigChanged = false;
  checkServerArgBoolean("webAuthentication", "Web Authentication", "on", "off", webConfigChanged, current_config.webAuthentication);
  checkServerArgString("webUsername", "webUsername", webConfigChanged, current_config.webUsername);
  checkServerArgString("webPassword", "webPassword", webConfigChanged, current_config.webPassword);
  if (webConfigChanged) {
    changed = true;
    setWebAuthentication(current_config.webAuthentication);
    setWebUserName(current_config.webUsername);
    setWebPassword(current_config.webPassword);
  }
  saveToSpiffsIfChanged(changed);
  
  ledManager.recalculateVariables();

  // -----------------------------------------------------------------------------
  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("General options");

  // 12/24 mode
  response_message += getRadioGroupHeader("12H/24H mode:");
  if (current_config.hourMode) {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", true);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", false);
  } else {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", false);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", true);
  }
  response_message += getRadioGroupFooter();

  // blank leading
  response_message += getRadioGroupHeader("Blank leading zero:");
  if (current_config.blankLeading) {
    response_message += getRadioButton("blankLeading", "Blank", "blank", true);
    response_message += getRadioButton("blankLeading", "Show", "show", false);
  } else {
    response_message += getRadioButton("blankLeading", "Blank", "blank", false);
    response_message += getRadioButton("blankLeading", "Show", "show", true);
  }
  response_message += getRadioGroupFooter();

  // Date format
  response_message += getDropDownHeader("Date format:", "dateFormat", false, false);
  response_message += getDropDownOption("0", "YY-MM-DD", (current_config.dateFormat == DATE_FORMAT_YYMMDD));
  response_message += getDropDownOption("1", "MM-DD-YY", (current_config.dateFormat == DATE_FORMAT_MMDDYY));
  response_message += getDropDownOption("2", "DD-MM-YY", (current_config.dateFormat == DATE_FORMAT_DDMMYY));
  response_message += getDropDownFooter();

  // Slots Mode
  response_message += getDropDownHeader("Date Slots:", "slotsMode", true, false);
  response_message += getDropDownOption("0", "Don't use slots mode", (current_config.slotsMode == 0));
  response_message += getDropDownOption("1", "Wipe In, Wipe Out", (current_config.slotsMode == 1));
  response_message += getDropDownOption("2", "Bang In, Bang Out", (current_config.slotsMode == 2));
  response_message += getDropDownFooter();

#ifdef FEATURE_PREHEAT
  // preheat mode
  response_message += getDropDownHeader("Digit pre-heat:", "preheatStrength", true, false);
  response_message += getDropDownOption("1", "Very Strong", (current_config.preheatStrength == 1));
  response_message += getDropDownOption("2", "Strong", (current_config.preheatStrength == 2));
  response_message += getDropDownOption("3", "Weak", (current_config.preheatStrength == 3));
  response_message += getDropDownOption("4", "Very Weak", (current_config.preheatStrength == 4));
  response_message += getDropDownOption("5", "Off", (current_config.preheatStrength == 5));
  response_message += getDropDownFooter();
#endif

#ifdef FEATURE_SCROLL
  // Scrollback
  response_message += getRadioGroupHeader("Scrollback effect:");
  if (current_config.scrollback) {
    response_message += getRadioButton("scrollback", "On", "on", true);
    response_message += getRadioButton("scrollback", "Off", "off", false);
  } else {
    response_message += getRadioButton("scrollback", "On", "on", false);
    response_message += getRadioButton("scrollback", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // Scroll steps
  response_message += getNumberInput("Scroll steps:", "scrollSteps", SCROLL_STEPS_MIN, SCROLL_STEPS_MAX, current_config.scrollSteps, !current_config.scrollback);
#endif

#ifdef FEATURE_FADE
  // fade
  response_message += getRadioGroupHeader("Fade effect:");
  if (current_config.fade) {
    response_message += getRadioButton("fade", "On", "on", true);
    response_message += getRadioButton("fade", "Off", "off", false);
  } else {
    response_message += getRadioButton("fade", "On", "on", false);
    response_message += getRadioButton("fade", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // Fade steps
  response_message += getNumberInput("Fade steps:", "fadeSteps", FADE_STEPS_MIN, FADE_STEPS_MAX, current_config.fadeSteps, !current_config.fade);
#endif

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Light Dependent Resistor");

  // LDR
  response_message += getRadioGroupHeader("Use LDR:");
  if (current_config.useLDR) {
    response_message += getRadioButton("useLDR", "On", "on", true);
    response_message += getRadioButton("useLDR", "Off", "off", false);
  } else {
    response_message += getRadioButton("useLDR", "On", "on", false);
    response_message += getRadioButton("useLDR", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // Min dim
  response_message += getNumberInput("Min Dim:", "minDim", MIN_DIM_MIN, MIN_DIM_MAX, current_config.minDim, false);

  // LDR threshold
  response_message += getNumberInput("LDR Threshold:", "thresholdBright", SENSOR_THRSH_MIN, SENSOR_THRSH_MAX, current_config.thresholdBright, false);

  // LDR Sensitivity
  response_message += getNumberInput("LDR Sensitivity:", "sensitivityLDR", SENSOR_SENSIT_MIN, SENSOR_SENSIT_MAX, current_config.sensitivityLDR, false);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Digit blanking");
  
#ifdef FEATURE_PIR
  if (pirInstalled) {
    response_message += getExplanationText("Motion detector installed - time based blanking is disabled");
  } else {
    response_message += getExplanationText("Motion detector not installed - motion detector blanking is inactive");
  }
  
  // PIR timeout
  response_message += getNumberInput("Motion timeout:", "pirTimeout", PIR_TIMEOUT_MIN, PIR_TIMEOUT_MAX, current_config.pirTimeout, !pirInstalled);

  // PIR pullup
  response_message += getRadioGroupHeader("Use Motion detector pullup:");
  if (current_config.usePIRPullup) {
    response_message += getRadioButton("usePIRPullup", "On", "on", true);
    response_message += getRadioButton("usePIRPullup", "Off", "off", false);
  } else {
    response_message += getRadioButton("usePIRPullup", "On", "on", false);
    response_message += getRadioButton("usePIRPullup", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
#endif

  // Day blanking
  response_message += getDropDownHeader("Day blanking:", "dayBlanking", true, pirInstalled);
  response_message += getDropDownOption("0", "Never blank", (current_config.dayBlanking == 0));
  response_message += getDropDownOption("1", "Blank all day on weekends", (current_config.dayBlanking == 1));
  response_message += getDropDownOption("2", "Blank all day on week days", (current_config.dayBlanking == 2));
  response_message += getDropDownOption("3", "Blank always", (current_config.dayBlanking == 3));
  response_message += getDropDownOption("4", "Blank during selected hours every day", (current_config.dayBlanking == 4));
  response_message += getDropDownOption("5", "Blank during selected hours on week days and all day on weekends", (current_config.dayBlanking == 5));
  response_message += getDropDownOption("6", "Blank during selected hours on weekends and all day on week days", (current_config.dayBlanking == 6));
  response_message += getDropDownOption("7", "Blank during selected hours on weekends only", (current_config.dayBlanking == 7));
  response_message += getDropDownOption("8", "Blank during selected hours on week days only", (current_config.dayBlanking == 8));
  response_message += getDropDownFooter();

  // Blank Mode
  response_message += getDropDownHeader("Blank Mode:", "blankMode", true, false);
  response_message += getDropDownOption("0", "Blank tubes only", (current_config.blankMode == 0));
  response_message += getDropDownOption("1", "Blank LEDs only", (current_config.blankMode == 1));
  response_message += getDropDownOption("2", "Blank tubes and LEDs", (current_config.blankMode == 2));
  response_message += getDropDownFooter();

  boolean hoursDisabled = (current_config.dayBlanking < 4) || pirInstalled;

  // Blank hours from
  response_message += getNumberInput("Blank from:", "blankHourStart", 0, 23, current_config.blankHourStart, hoursDisabled);

  // Blank hours to
  response_message += getNumberInput("Blank to:", "blankHourEnd", 0, 23, current_config.blankHourEnd, hoursDisabled);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Back/Underlights");

  // Back light
  response_message += getDropDownHeader("Back light:", "backlightMode", true, false);
  response_message += getDropDownOption("0", "Fixed RGB backlight", (current_config.backlightMode == 0));
  response_message += getDropDownOption("1", "Cycling RGB backlight", (current_config.backlightMode == 1));
  response_message += getDropDownOption("2", "'Colourtime' backlight", (current_config.backlightMode == 2));
  response_message += getDropDownOption("3", "'Day of week' backlight", (current_config.backlightMode == 3));
  response_message += getDropDownFooter();

  // Cycle speed
  boolean activeCycle = (current_config.backlightMode == 1);
  response_message += getNumberInput("Backlight Cycle Speed:", "cycleSpeed", CYCLE_SPEED_MIN, CYCLE_SPEED_MAX, current_config.cycleSpeed, !activeCycle);

  // Dim backlights
  response_message += getRadioGroupHeader("Dim backlights:");
  if (current_config.useBLDim) {
    response_message += getRadioButton("useBLDim", "On", "on", true);
    response_message += getRadioButton("useBLDim", "Off", "off", false);
  } else {
    response_message += getRadioButton("useBLDim", "On", "on", false);
    response_message += getRadioButton("useBLDim", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // Pulse backlights
  response_message += getRadioGroupHeader("Pulse backlights:");
  if (current_config.useBLPulse) {
    response_message += getRadioButton("useBLPulse", "On", "on", true);
    response_message += getRadioButton("useBLPulse", "Off", "off", false);
  } else {
    response_message += getRadioButton("useBLPulse", "On", "on", false);
    response_message += getRadioButton("useBLPulse", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // RGB channels
  boolean activeLEDs = (current_config.backlightMode == 0);

  response_message += getNumberInput("Red intensity:", "redCnl", 0, 15, current_config.redCnl, !activeLEDs);
  response_message += getNumberInput("Green intensity:", "grnCnl", 0, 15, current_config.grnCnl, !activeLEDs);
  response_message += getNumberInput("Blue intensity:", "bluCnl", 0, 15, current_config.bluCnl, !activeLEDs);

  // backlight dim factor
  response_message += getNumberInput("Backlight Dim:", "backlightDimFactor", BACKLIGHT_DIM_FACTOR_MIN, BACKLIGHT_DIM_FACTOR_MAX, current_config.backlightDimFactor, false);

#ifdef FEATURE_EXT_LEDS
  // external backlight dim factor
  response_message += getNumberInput("Underlight Dim:", "extDimFactor", EXT_DIM_FACTOR_MIN, EXT_DIM_FACTOR_MAX, current_config.extDimFactor, false);
#endif

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Separators");

#ifdef FEATURE_LED_MODES
  // LED mode
  response_message += getDropDownHeader("LED Mode:", "ledMode", true, false);
  response_message += getDropDownOption("0", "RailRoad", (current_config.ledMode == LED_RAILROAD));
  response_message += getDropDownOption("1", "Slow flash", (current_config.ledMode == LED_BLINK_SLOW));
  response_message += getDropDownOption("2", "Fast flash", (current_config.ledMode == LED_BLINK_FAST));
  response_message += getDropDownOption("3", "Double flash", (current_config.ledMode == LED_BLINK_DBL));
  response_message += getDropDownOption("4", "Always On", (current_config.ledMode == LED_ON));
  response_message += getDropDownOption("5", "Always Off", (current_config.ledMode == LED_OFF));
  response_message += getDropDownFooter();
#endif

#ifdef FEATURE_STATUS_LEDS
  // Status LED mode Left
  response_message += getDropDownHeader("Left Status LED Mode:", "statusModeL", true, false);
  response_message += getDropDownOption("0", "NTP", (current_config.statusModeL == STATUS_LED_MODE_NTP));
  response_message += getDropDownOption("1", "Connected", (current_config.statusModeL == STATUS_LED_MODE_ONLINE));
  response_message += getDropDownOption("2", "AM/PM", (current_config.statusModeL == STATUS_LED_MODE_AM_PM));
  response_message += getDropDownOption("3", "PIR", (current_config.statusModeL == STATUS_LED_MODE_PIR));
  response_message += getDropDownOption("4", "Off", (current_config.statusModeL == STATUS_LED_MODE_OFF));
  response_message += getDropDownOption("5", "On", (current_config.statusModeL == STATUS_LED_MODE_ON));
  response_message += getDropDownFooter();

  // Status LED mode Right
  response_message += getDropDownHeader("Right Status LED Mode:", "statusModeR", true, false);
  response_message += getDropDownOption("0", "NTP", (current_config.statusModeR == STATUS_LED_MODE_NTP));
  response_message += getDropDownOption("1", "Connected", (current_config.statusModeR == STATUS_LED_MODE_ONLINE));
  response_message += getDropDownOption("2", "AM/PM", (current_config.statusModeR == STATUS_LED_MODE_AM_PM));
  response_message += getDropDownOption("3", "PIR", (current_config.statusModeR == STATUS_LED_MODE_PIR));
  response_message += getDropDownOption("4", "Off", (current_config.statusModeR == STATUS_LED_MODE_OFF));
  response_message += getDropDownOption("5", "On", (current_config.statusModeR == STATUS_LED_MODE_ON));
  response_message += getDropDownFooter();
#endif

  response_message += getDropDownHeader("Separator Dimming:", "separatorDimFactor", true, false);
  response_message += getDropDownOption("1", "Bright", (current_config.separatorDimFactor == SEP_BRIGHT));
  response_message += getDropDownOption("2", "Dim", (current_config.separatorDimFactor == SEP_DIM));
  response_message += getDropDownFooter();

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Security");

  // web auth mode
  response_message += getRadioGroupHeader("Web Authentication:");
  if (current_config.webAuthentication) {
    response_message += getRadioButton("webAuthentication", " On", "on", true);
    response_message += getRadioButton("webAuthentication", " Off", "off", false);
  } else {
    response_message += getRadioButton("webAuthentication", " On", "on", false);
    response_message += getRadioButton("webAuthentication", " Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  response_message += getTextInput("User Name", "webUsername", current_config.webUsername, !current_config.webAuthentication);
  response_message += getTextInput("Password", "webPassword", current_config.webPassword, !current_config.webAuthentication);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // all done
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Config page out");
}

// ************************************************************
// Send a value to the clock for display
// ************************************************************
void setDisplayValuePageHandler() {
  String resultMessage;
  byte timeValue = 0xff;
  boolean valueValid = false;
  boolean timeValid = false;
  boolean formatValid = false;

  int valueToDisplay = 0;
  int valueTime = 0;
  int valueFormat = 0;

  checkServerArgInt("value", "value", valueValid, valueToDisplay);
  checkServerArgInt("time", "time", timeValid, valueTime);
  checkServerArgInt("format", "format", formatValid, valueFormat);

  if (valueValid) {
    OutputManager::Instance().setValueToShow(valueToDisplay % 1000000);
    OutputManager::Instance().setValueDisplayTime(10);
  }

  if (timeValid) {
    if (valueTime >= 255) {
      OutputManager::Instance().setValueDisplayTime(255);
    } else {
      OutputManager::Instance().setValueDisplayTime(valueTime);
    }
  }

  if (formatValid) {
    OutputManager::Instance().setValueFormat(valueFormat);
  }

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Show a value on the clock</h3>";

  if (valueValid) {
    response_message += "<div class=\"alert alert-success fade in\"><strong>";
    response_message += "display value was set!";
    response_message += "</strong></div></div>";
  } else {
    response_message += "<div class=\"alert alert-error fade in\"><strong>";
    response_message += "You need to set at least the \"value\" parameter! A valid command line is http://<clock.ip.addres>/setvalue?value=123456&time=60&format=333333";
    response_message += "</strong><br>";
    response_message += "The format values are: 0 = blanked digit, 1 = normal display, 2 = blinking digit</div></div>";
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ************************************************************
// Access to utility functions
// ************************************************************
void utilityPageHandler()
{
  debugManager.debugMsg("Utility page in");


  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Clock Utilities");

  response_message += "<ul>";
  response_message += "<hr><li><a href=\"/resetwifi\">Clear WiFi configuration and restart</a></li>";
  response_message += "<hr><li><a href=\"/reset\">Restart module</a></li>";
  response_message += "<hr><li><a href=\"/update\">Update firmware</a></li>";
  response_message += "<hr><li><a href=\"/ntpupdate\">Force update from NTP now</a></li>";
  response_message += "<hr><li><a href=\"/factoryreset\">Perform factory reset without resetting Wifi configuration</a></li>";
  response_message += "</ul>";

  response_message += getHTMLFoot();
  server.send(200, "text/html", response_message);
  debugManager.debugMsg("Utility page out");
}

// ************************************************************
// Reset just the wifi
// ************************************************************
void resetWiFiPageHandler() {
  debugManager.debugMsg("Reset WiFi page in");

  WiFiManager wifiManager;
  wifiManager.resetSettings();

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();
  response_message += "<div class=\"alert alert-success fade in\"><strong>Attempting a restart.</strong>";
  response_message += "<script> var timer = setTimeout(function() {window.location='/'}, 10000);</script>";
  response_message += getHTMLFoot();
  server.send(200, "text/html", response_message);
  debugManager.debugMsg("Reset WiFi page out");
  delay(1000);    // wait to deliver response
  ESP.restart();  // should reboot
}

// ************************************************************
// Do an NTP update now
// ************************************************************
void ntpUpdatePageHandler() {
  debugManager.debugMsg("NTP Update page in");

  ntpAsync.getTimeFromNTP();

  server.sendHeader("Location", "/utility", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("NTP Update page out");
}

// ************************************************************
// Reset all the settings
// ************************************************************
void factoryResetPageHandler() {
  debugManager.debugMsg("Factory Reset page in");

  factoryReset();

  server.sendHeader("Location", "/utility", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Factory Reset page out");
}

// ************************************************************
// Reset just the wifi
// ************************************************************
void debugPageHandler() {
  debugManager.debugMsg("Debug page in");

  debugManager.setUp(true);

  server.sendHeader("Location", "/utility", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Debug page out");
}

// ************************************************************
// Called if requested page is not found
// ************************************************************
void handleNotFound() {
  debugManager.debugMsg("404 page in");

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);

  debugManager.debugMsg("404 page out");
}

// ************************************************************
// Called if we need to have a local CSS
// ************************************************************
void localCSSHandler()
{
  PGM_P cssStr = PSTR(".navbar,.table{margin-bottom:20px}.nav>li,.nav>li>a,article,aside,details,figcaption,figure,footer,header,hgroup,main,menu,nav,section,summary{display:block}.btn,.form-control,.navbar-toggle{background-image:none}.table,label{max-width:100%}.sub-header{padding-bottom:10px;border-bottom:1px solid #eee}.h3,h3{font-size:24px}.table{width:100%}table{background-color:transparent;border-spacing:0;border-collapse:collapse}.table-striped>tbody>tr:nth-of-type(2n+1){background-color:#f9f9f9}.table>caption+thead>tr:first-child>td,.table>caption+thead>tr:first-child>th,.table>colgroup+thead>tr:first-child>td,.table>colgroup+thead>tr:first-child>th,.table>thead:first-child>tr:first-child>td,.table>thead:first-child>tr:first-child>th{border-top:0}.table>thead>tr>th{border-bottom:2px solid #ddd}.table>tbody>tr>td,.table>tbody>tr>th,.table>tfoot>tr>td,.table>tfoot>tr>th,.table>thead>tr>td,.table>thead>tr>th{padding:8px;line-height:1.42857143;vertical-align:top;border-top:1px solid #ddd}th{text-align:left}td,th{padding:0}.navbar>.container .navbar-brand,.navbar>.container-fluid .navbar-brand{margin-left:-15px}.container,.container-fluid{padding-right:15px;padding-left:15px;margin-right:auto;margin-left:auto}.navbar-inverse .navbar-brand{color:#9d9d9d}.navbar-brand{float:left;height:50px;padding:15px;font-size:18px;line-height:20px}a{color:#337ab7;text-decoration:none;background-color:transparent}.navbar-fixed-top{border:0;top:0;border-width:0 0 1px}.navbar-inverse{background-color:#222;border-color:#080808}.navbar-fixed-bottom,.navbar-fixed-top{border-radius:0;position:fixed;right:0;left:0;z-index:1030}.nav>li,.nav>li>a,.navbar,.navbar-toggle{position:relative}.navbar{border-radius:4px;min-height:50px;border:1px solid transparent}.container{width:750px}.navbar-right{float:right!important;margin-right:-15px}.navbar-nav{float:left;margin:7.5px -15px}.nav{padding-left:0;margin-bottom:0;list-style:none}.navbar-nav>li{float:left}.navbar-inverse .navbar-nav>li>a{color:#9d9d9d}.navbar-nav>li>a{padding-top:10px;padding-bottom:10px;line-height:20px}.nav>li>a{padding:10px 15px}.navbar-inverse .navbar-toggle{border-color:#333}.navbar-toggle{display:none;float:right;padding:9px 10px;margin-top:8px;margin-right:15px;margin-bottom:8px;background-color:transparent;border:1px solid transparent;border-radius:4px}button,select{text-transform:none}button{overflow:visible}button,html input[type=button],input[type=reset],input[type=submit]{-webkit-appearance:button;cursor:pointer}.btn-primary{color:#fff;background-color:#337ab7;border-color:#2e6da4}.btn{display:inline-block;padding:6px 12px;margin-bottom:0;font-size:14px;font-weight:400;line-height:1.42857143;text-align:center;white-space:nowrap;vertical-align:middle;-ms-touch-action:manipulation;touch-action:manipulation;cursor:pointer;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;border-radius:4px}button,input,select,textarea{font-family:inherit;font-size:inherit;line-height:inherit}input{line-height:normal}button,input,optgroup,select,textarea{margin:0;font:inherit;color:inherit}.form-control,body{font-size:14px;line-height:1.42857143}.form-horizontal .form-group{margin-right:-15px;margin-left:-15px}.form-group{margin-bottom:15px}.form-horizontal .control-label{padding-top:7px;margin-bottom:0;text-align:right}.form-control{display:block;width:100%;height:34px;padding:6px 12px;color:#555;background-color:#fff;border:1px solid #ccc;border-radius:4px;-webkit-box-shadow:inset 0 1px 1px rgba(0,0,0,.075);box-shadow:inset 0 1px 1px rgba(0,0,0,.075);-webkit-transition:border-color ease-in-out .15s,-webkit-box-shadow ease-in-out .15s;-o-transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s;transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s}.col-xs-8{width:66.66666667%}.col-xs-3{width:25%}.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{float:left}.col-lg-1,.col-lg-10,.col-lg-11,.col-lg-12,.col-lg-2,.col-lg-3,.col-lg-4,.col-lg-5,.col-lg-6,.col-lg-7,.col-lg-8,.col-lg-9,.col-md-1,.col-md-10,.col-md-11,.col-md-12,.col-md-2,.col-md-3,.col-md-4,.col-md-5,.col-md-6,.col-md-7,.col-md-8,.col-md-9,.col-sm-1,.col-sm-10,.col-sm-11,.col-sm-12,.col-sm-2,.col-sm-3,.col-sm-4,.col-sm-5,.col-sm-6,.col-sm-7,.col-sm-8,.col-sm-9,.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{position:relative;min-height:1px;padding-right:15px;padding-left:15px}label{display:inline-block;margin-bottom:5px;font-weight:700}*{-webkit-box-sizing:border-box;-moz-box-sizing:border-box;box-sizing:border-box}body{font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;color:#333}html{font-size:10px;font-family:sans-serif;-webkit-text-size-adjust:100%}");
  server.send(200, "text/css", String(cssStr));
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Network handling -----------------------------------------
// ----------------------------------------------------------------------------------------------------

// ************************************************************
// What it says on the tin
// ************************************************************
boolean getIsConnected() {
  return WiFi.isConnected();
}

// ************************************************************
// Tell the Diag LED that we have connected
// ************************************************************
void configModeCallback(WiFiManager *myWiFiManager) {
  debugManager.debugMsg("*** Entered config mode");
  setDiagnosticLED(DIAGS_WIFI, STATUS_BLUE);
  OutputManager::Instance().loadNumberArrayPOSTMessage(AP_MODE);
  OutputManager::Instance().outputDisplay();
}

// ************************************************************
// In the case that we have lost the WiFi connection, try to reconnect
// ************************************************************
void reconnectDroppedConnection() {
  // Try to reconnect if we are  not connected
  if (WiFi.status() != WL_CONNECTED) {
    debugManager.debugMsg("Attepting to reconnect dropped connection WiFi connection to " + WiFi.SSID());
    ETS_UART_INTR_DISABLE();
    wifi_station_disconnect();
    ETS_UART_INTR_ENABLE();
    WiFi.begin();
  }
}

// ************************************************************
// set up the server page handlers
// ************************************************************
void setServerPageHandlers() {
  server.on("/", rootPageHandler);

  server.on("/time", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return timeServerPageHandler();
  });

  server.on("/reset", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return resetPageHandler();
  });

  server.on("/resetwifi", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return resetWiFiPageHandler();
  });

  server.on("/ntpupdate", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return ntpUpdatePageHandler();
  });

  server.on("/factoryreset", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return factoryResetPageHandler();
  });

  server.on("/clockconfig", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return clockConfigPageHandler();
  });

  server.on("/setvalue", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return setDisplayValuePageHandler();
  });

  server.on("/utility", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return utilityPageHandler();
  });

  server.on("/debug", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return debugPageHandler();
  });

  server.on("/local.css",   localCSSHandler);

  server.onNotFound(handleNotFound);
}

void debugMsgLocal(String message) {
  debugManager.debugMsg(message);
}
