#include "OutputManagerMicrochip6.h"
#include "TimeLib.h"
#include "ClockUtils.h"

// ************************************************************
// Instance value
// ************************************************************
OutputManager* OutputManager::pInstance;

// ************************************************************
// Singleton accessor
// ************************************************************
OutputManager& OutputManager::Instance(){
  return *OutputManager::pInstance;
}

// ************************************************************
// Singleton creator
// ************************************************************
void OutputManager::CreateInstance(){
  if (OutputManager::pInstance == nullptr) {
    OutputManager::pInstance = new OutputManager();
  }
}

// ************************************************************
// Set up the manager
// ************************************************************
void OutputManager::setUp() {
  pinMode(LATCHPin, OUTPUT);
  digitalWrite(LATCHPin, LOW);
  pinMode(CLOCKPin, OUTPUT);
  pinMode(DATAPin, OUTPUT);
  
  pinMode(BLANKPin, OUTPUT);  
  digitalWrite(BLANKPin, HIGH);

  setLDRValue(0);
  setBlankingPin();
}

// ************************************************************
// Set up the manager
// ************************************************************
void OutputManager::setConfigObject(spiffs_config_t* ccPtr) {
  cc = ccPtr;
}

// ************************************************************
// Do a single complete display, including any fading and
// dimming requested. Performs the display loop
// DIGIT_DISPLAY_COUNT times for each digit, with no delays.
// This is the heart of the display processing!
// ************************************************************
void OutputManager::outputDisplay() {
  int tmpDispType;

  // Deal with blink, calculate if we are on or off
  _blinkCounter--;
  if (_blinkCounter <= 0) {
    _blinkState = !_blinkState;
    _blinkCounter = _blinkState ? BLINK_COUNT_ON : BLINK_COUNT_OFF;
  }
  
  for ( int i = 0 ; i < DIGIT_COUNT ; i ++ ) {
    tmpDispType = _digit_buffer.displayType[i]; 
    if (blankTubes) {
      tmpDispType = BLANKED;
//    } else if ((_digit_buffer.numberArray[i] != _digit_buffer.currentNumberArray[i]) && !transition.isMessageOnDisplay(nowMillis)) {
    } else if (_digit_buffer.numberArray[i] != _digit_buffer.currentNumberArray[i]) {
      if ((_digit_buffer.numberArray[i] == 0) && cc->scrollback) {
        tmpDispType = SCROLL;
      } else if (cc->fade) {
        tmpDispType = FADE;
      }
    }

    // --------------------- Scroll ---------------------

    // manage scrolling, each impression we show 1 fade step less of the old
    // digit and 1 fade step more of the new. We hold the 
    if (tmpDispType == SCROLL) {
      if (_digit_buffer.numberArray[i] != _digit_buffer.currentNumberArray[i]) {
        if (_digit_buffer.fadeState[i] == 0) {
          // Start the scroll step
          _digit_buffer.fadeState[i] = cc->scrollSteps;
        }

        if (_digit_buffer.fadeState[i] == 1) {
          // finish the scroll step
          _digit_buffer.fadeState[i] = 0;
          _digit_buffer.currentNumberArray[i] = _digit_buffer.currentNumberArray[i] - 1;
        } else if (_digit_buffer.fadeState[i] > 1) {
          // Continue the scroll countdown
          _digit_buffer.fadeState[i] = _digit_buffer.fadeState[i] - 1;
        }
      }
    } else

    // --------------------- Fade ---------------------

    // manage fading, each step we show 1 fade step less of the old
    // digit and 1 fade step more of the new
    if (tmpDispType == FADE) {
      if (_digit_buffer.numberArray[i] != _digit_buffer.currentNumberArray[i]) {
        // Start the fade
        if (_digit_buffer.fadeState[i] == 0) {
          _digit_buffer.fadeState[i] = cc->fadeSteps;
          // debugMsg("Start Fade");
        }
      }

      if (_digit_buffer.fadeState[i] == 1) {
        // finish the fade
        _digit_buffer.fadeState[i] = 0;
        _digit_buffer.currentNumberArray[i] = _digit_buffer.numberArray[i];
      } else if (_digit_buffer.fadeState[i] > 1) {
        // Continue the fade
        _digit_buffer.fadeState[i] = _digit_buffer.fadeState[i] - 1;
      }
    }

    switch (tmpDispType) {
      case BLANKED:
        {
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], COUNTS_PER_DIGIT, 0, true);
          break;
        }
      case DIMMED:
        {
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], COUNTS_PER_DIGIT_DIM, 0, false);
          break;
        }
      case BRIGHT:
        {
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], COUNTS_PER_DIGIT, 0, false);
          break;
        }
      case NORMAL:
        {
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], _ldrValue, 0, false);
          break;
        }
      case FADE:
        {
          byte switchTime = getSwitchTime(_ldrValue, _digit_buffer.fadeState[i], cc->fadeSteps);
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], _ldrValue, switchTime, false);
          break;
        }
      case SCROLL:
        {
          // Set the Switch state to 1 to show the previous digit
          setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], _ldrValue, 1, false);
          break;
        }
      case BLINK:
        {
          if (_blinkState) {
            setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], _ldrValue, 0, false);
          } else {
            setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], COUNTS_PER_DIGIT, 0, true);
          }
          break;
        }
    }
  }
}

// ************************************************************
// Calculate the off time for this digit
// fadeSteps: The number of iterations we should fade over
// fadeState: the current step from 0 to fadeState-1
// result: 1..offCount
// ************************************************************
int OutputManager::getSwitchTime(byte offCount, byte fadeState, byte fadeSteps) {
  float digitSwitchTimeFloat;
  digitSwitchTimeFloat = (float) offCount * (float) (fadeSteps - fadeState) / (float) fadeSteps;
  byte result = (byte) digitSwitchTimeFloat;

  // can't give back 0, because that means ("show new")
  if (result == 0) result = 1;
  // debugMsg("fadeState: " + String(fadeState) + ", result: " + result);
  return result;
}

// ************************************************************
// Set the schedule and display values for a single digit, used in
// display preparation, not needed during at interrupt time
//
// digit: The digit to set 0..(DIGIT_COUNT-1)
// value: The value to show
// prevValue: the value we are fading from
// dimFactor:   1..COUNTS_PER_DIGIT : COUNTS_PER_DIGIT = not dimmed
// switchTime: 0..COUNTS_PER_DIGIT-1 : 0 = no switch (undimmed)
// switchTime: 0..dimValue-1 : 0 = no switch (dimmed)
// ************************************************************
void OutputManager::setDigitBuffers(byte digit, byte value, byte prevValue, byte dimFactor, byte switchTime, bool blanked) {
  if (dimFactor < 1) {
    dimFactor = 1; 
  }

  if (dimFactor > COUNTS_PER_DIGIT) {
    dimFactor = COUNTS_PER_DIGIT;
  }

  if (switchTime >= dimFactor) {
    switchTime = dimFactor - 1;
    if (switchTime < 1) {
      // can't show fading when we are so dim
      switchTime = 0;
    }
  }

  // calculate the new column
  uint32_t newVals[COUNTS_PER_DIGIT];
  uint32_t currColVal = 0;
  for (int idx = 0 ; idx < COUNTS_PER_DIGIT ; idx++) {
    if (!blanked) {
      if (idx == 0) {
        currColVal = DECODE_DIGIT[value%10];
      } else if (idx == switchTime) {
        currColVal = DECODE_DIGIT[prevValue%10];
      } else if (idx == dimFactor) {
        currColVal = 0;
      }
    }
    newVals[idx] = currColVal;
  }
  
  // merge in, shifting as necessary
  for (int idx = 0 ; idx < COUNTS_PER_DIGIT ; idx++) {
    switch (digit) {
      case 3: {
          valueBufferCurr1[idx] = valueBufferCurr1[idx] & 0x3ffffc00 | newVals[idx];
          break;
      }
      case 4: {
          valueBufferCurr1[idx] = valueBufferCurr1[idx] & 0x3ff003ff | newVals[idx] << 10;
          break;
      }
      case 5: {
          valueBufferCurr1[idx] = valueBufferCurr1[idx] & 0xc00fffff | newVals[idx] << 20;
          break;
      }
      case 0: {
          valueBufferCurr2[idx] = valueBufferCurr2[idx] & 0x3ffffc00 | newVals[idx];
          break;
      }
      case 1: {
          valueBufferCurr2[idx] = valueBufferCurr2[idx] & 0x3ff003ff | newVals[idx] << 10;
          break;
      }
      case 2: {
          valueBufferCurr2[idx] = valueBufferCurr2[idx] & 0xc00fffff | newVals[idx] << 20;
          break;
      }
    }

    // merge in the LEDs
    if (cc->separatorDimFactor == 2) {
      if (idx < dimFactor/4) {
        valueBufferCurr1[idx] |= DECODE_LED[led1State];
        valueBufferCurr2[idx] |= DECODE_LED[led2State];
      }
    } else {
      if (idx < dimFactor) {
        valueBufferCurr1[idx] |= DECODE_LED[led1State];
        valueBufferCurr2[idx] |= DECODE_LED[led2State];
      }
    }
  }
  
}

// ************************************************************
// Set the 48 output bits on the shift registers without trying
// to interpret them as numbers. This is used when displaying
// non-numerical values.
// ************************************************************
void OutputManager::outputDisplayDiags() {
  // No need to do anything special for this display type
  // Just do the usual thing
  outputDisplay();
}

// ************************************************************
// set the PWM Brighness
// The HV drivers have a blanking pin which is active low
// so we have to invert the sense of the ldr value
// ************************************************************
void OutputManager::setLDRValue(unsigned int newBrightness) {
  if (_ldrValue != newBrightness) {
    _ldrValue = newBrightness;
  }
}

// ************************************************************
// Set the blanking pin to show the display. If we have gone
// into display blanking mode, slowly fade out the display to
// completely off.
// ************************************************************
void OutputManager::setBlankingPin() {
  if (!blankTubes) {
    _lastLDRValue = _ldrValue;
  } else {
    // Fade out slowly
    if (_lastLDRValue < 1023) {
      _lastLDRValue++;
    }
  }

  // The HV blanking pin is active low, so invert the value
  //analogWrite(BLANKPin, 1023 - _lastLDRValue);
}

//**********************************************************************************
//**********************************************************************************
//*                             Utility functions                                  *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void OutputManager::loadNumberArrayTime() {
  _digit_buffer.numberArray[5] = second() % 10;
  _digit_buffer.numberArray[4] = second() / 10;
  _digit_buffer.numberArray[3] = minute() % 10;
  _digit_buffer.numberArray[2] = minute() / 10;
  if (cc->hourMode) {
    _digit_buffer.numberArray[1] = hourFormat12() % 10;
    _digit_buffer.numberArray[0] = hourFormat12() / 10;
  } else {
    _digit_buffer.numberArray[1] = hour() % 10;
    _digit_buffer.numberArray[0] = hour() / 10;
  }
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void OutputManager::loadNumberArraySameValue(byte val) {
  _digit_buffer.numberArray[5] = val;
  _digit_buffer.numberArray[4] = val;
  _digit_buffer.numberArray[3] = val;
  _digit_buffer.numberArray[2] = val;
  _digit_buffer.numberArray[1] = val;
  _digit_buffer.numberArray[0] = val;
}

// ************************************************************
// Display preset - used for Power On Self Test
// ************************************************************
void OutputManager::loadNumberArrayPOSTMessage(int postValue) {
  loadNumberArrayConfIntWide(postValue);

  // Load manually into the display buffer - the display loop is not working yet
  for (int i = 0 ; i < DIGIT_COUNT ; i++) {
    setDigitBuffers(i, _digit_buffer.numberArray[i], _digit_buffer.currentNumberArray[i], COUNTS_PER_DIGIT, 0, false);
  }
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void OutputManager::loadNumberArrayDate() {
  switch (cc->dateFormat) {
    case DATE_FORMAT_YYMMDD:
      _digit_buffer.numberArray[5] = day() % 10;
      _digit_buffer.numberArray[4] = day() / 10;
      _digit_buffer.numberArray[3] = month() % 10;
      _digit_buffer.numberArray[2] = month() / 10;
      _digit_buffer.numberArray[1] = (year() - 2000) % 10;
      _digit_buffer.numberArray[0] = (year() - 2000) / 10;
      break;
    case DATE_FORMAT_MMDDYY:
      _digit_buffer.numberArray[5] = (year() - 2000) % 10;
      _digit_buffer.numberArray[4] = (year() - 2000) / 10;
      _digit_buffer.numberArray[3] = day() % 10;
      _digit_buffer.numberArray[2] = day() / 10;
      _digit_buffer.numberArray[1] = month() % 10;
      _digit_buffer.numberArray[0] = month() / 10;
      break;
    case DATE_FORMAT_DDMMYY:
      _digit_buffer.numberArray[5] = (year() - 2000) % 10;
      _digit_buffer.numberArray[4] = (year() - 2000) / 10;
      _digit_buffer.numberArray[3] = month() % 10;
      _digit_buffer.numberArray[2] = month() / 10;
      _digit_buffer.numberArray[1] = day() % 10;
      _digit_buffer.numberArray[0] = day() / 10;
      break;
  }
}

// ************************************************************
// Break the LDR reading into displayable digits
// ************************************************************
void OutputManager::loadNumberArrayLDR() {
  _digit_buffer.numberArray[5] = 0;
  _digit_buffer.numberArray[4] = 0;

  int ldrValueDisp = 1023 - _ldrValue;

  _digit_buffer.numberArray[3] = (ldrValueDisp / 1) % 10;
  _digit_buffer.numberArray[2] = (ldrValueDisp / 10) % 10;
  _digit_buffer.numberArray[1] = (ldrValueDisp / 100) % 10;
  _digit_buffer.numberArray[0] = (ldrValueDisp / 1000) % 10;
}

// ************************************************************
// Test digits
// ************************************************************
void OutputManager::loadNumberArrayTestDigits() {
  _digit_buffer.numberArray[5] =  second() % 10;
  _digit_buffer.numberArray[4] = (second() + 1) % 10;
  _digit_buffer.numberArray[3] = (second() + 2) % 10;
  _digit_buffer.numberArray[2] = (second() + 3) % 10;
  _digit_buffer.numberArray[1] = (second() + 4) % 10;
  _digit_buffer.numberArray[0] = (second() + 5) % 10;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void OutputManager::loadNumberArrayConfInt(int confValue, int confNum) {
  _digit_buffer.numberArray[5] = (confNum) % 10;
  _digit_buffer.numberArray[4] = (confNum / 10) % 10;
  _digit_buffer.numberArray[3] = (confValue / 1) % 10;
  _digit_buffer.numberArray[2] = (confValue / 10) % 10;
  _digit_buffer.numberArray[1] = (confValue / 100) % 10;
  _digit_buffer.numberArray[0] = (confValue / 1000) % 10;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void OutputManager::loadNumberArrayConfIntWide(int confValue) {
  _digit_buffer.numberArray[5] = (confValue / 1) % 10;
  _digit_buffer.numberArray[4] = (confValue / 10) % 10;
  _digit_buffer.numberArray[3] = (confValue / 100) % 10;
  _digit_buffer.numberArray[2] = (confValue / 1000) % 10;
  _digit_buffer.numberArray[1] = (confValue / 10000) % 10;
  _digit_buffer.numberArray[0] = (confValue / 100000) % 10;
}

// ************************************************************
// Show a boolean configuration value
// ************************************************************
void OutputManager::loadNumberArrayConfBool(boolean confValue, int confNum) {
  int boolInt;
  if (confValue) {
    boolInt = 1;
  } else {
    boolInt = 0;
  }
  _digit_buffer.numberArray[5] = (confNum) % 10;
  _digit_buffer.numberArray[4] = (confNum / 10) % 10;
  _digit_buffer.numberArray[3] = boolInt;
  _digit_buffer.numberArray[2] = 0;
  _digit_buffer.numberArray[1] = 0;
  _digit_buffer.numberArray[0] = 0;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void OutputManager::loadNumberArrayIP(byte byte1, byte byte2) {
  _digit_buffer.numberArray[5] = (byte2) % 10;
  _digit_buffer.numberArray[4] = (byte2 / 10) % 10;
  _digit_buffer.numberArray[3] = (byte2 / 100) % 10;
  _digit_buffer.numberArray[2] = (byte1) % 10;
  _digit_buffer.numberArray[1] = (byte1 / 10) % 10;
  _digit_buffer.numberArray[0] = (byte1 / 100) % 10;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void OutputManager::loadNumberArrayESPID(String ID) {
  byte byteArray[5];
  hexCharacterStringToBytes(byteArray, ID.c_str());
  _digit_buffer.numberArray[5] = byteArray[5];
  _digit_buffer.numberArray[4] = byteArray[4];
  _digit_buffer.numberArray[3] = byteArray[3];
  _digit_buffer.numberArray[2] = byteArray[2];
  _digit_buffer.numberArray[1] = byteArray[1];
  _digit_buffer.numberArray[0] = byteArray[0];
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void OutputManager::loadNumberArrayValueToShow() {
  _digit_buffer.numberArray[5] = (_value_buffer.valueToShow / 1) % 10;
  _digit_buffer.numberArray[4] = (_value_buffer.valueToShow / 10) % 10;
  _digit_buffer.numberArray[3] = (_value_buffer.valueToShow / 100) % 10;
  _digit_buffer.numberArray[2] = (_value_buffer.valueToShow / 1000) % 10;
  _digit_buffer.numberArray[1] = (_value_buffer.valueToShow / 10000) % 10;
  _digit_buffer.numberArray[0] = (_value_buffer.valueToShow / 100000) % 10;
}

// ************************************************************
// highlight years taking into account the date format
// ************************************************************
void OutputManager::highlightYearsDateFormat() {
  switch (cc->dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight0and1();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight4and5();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight4and5();
      break;
  }
}

// ************************************************************
// highlight years taking into account the date format
// ************************************************************
void OutputManager::highlightMonthsDateFormat() {
  switch (cc->dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight2and3();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight0and1();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight2and3();
      break;
  }
}

// ************************************************************
// highlight days taking into account the date format
// ************************************************************
void OutputManager::highlightDaysDateFormat() {
  switch (cc->dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight4and5();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight2and3();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight0and1();
      break;
  }
}

// ************************************************************
// Display preset, highlight digits 0 and 1
// ************************************************************
void OutputManager::highlight0and1() {
  _digit_buffer.displayType[0] = BLINK;
  _digit_buffer.displayType[1] = BLINK;
  _digit_buffer.displayType[2] = NORMAL;
  _digit_buffer.displayType[3] = NORMAL;
  _digit_buffer.displayType[4] = NORMAL;
  _digit_buffer.displayType[5] = NORMAL;
}

// ************************************************************
// Display preset, highlight digits 2 and 3
// ************************************************************
void OutputManager::highlight2and3() {
  _digit_buffer.displayType[0] = NORMAL;
  _digit_buffer.displayType[1] = NORMAL;
  _digit_buffer.displayType[2] = BLINK;
  _digit_buffer.displayType[3] = BLINK;
  _digit_buffer.displayType[4] = NORMAL;
  _digit_buffer.displayType[5] = NORMAL;
}

// ************************************************************
// Display preset, highlight digits 4 and 5
// ************************************************************
void OutputManager::highlight4and5() {
  _digit_buffer.displayType[0] = NORMAL;
  _digit_buffer.displayType[1] = NORMAL;
  _digit_buffer.displayType[2] = NORMAL;
  _digit_buffer.displayType[3] = NORMAL;
  _digit_buffer.displayType[4] = BLINK;
  _digit_buffer.displayType[5] = BLINK;
}

// ************************************************************
// Display preset
// ************************************************************
void OutputManager::allNormal(bool leadingBlank) {
  if (leadingBlank)
    applyBlanking();
  else 
    _digit_buffer.displayType[0] = NORMAL;

  _digit_buffer.displayType[1] = NORMAL;
  _digit_buffer.displayType[2] = NORMAL;
  _digit_buffer.displayType[3] = NORMAL;
  _digit_buffer.displayType[4] = NORMAL;
  _digit_buffer.displayType[5] = NORMAL;
}

// ************************************************************
// Display preset
// ************************************************************
void OutputManager::displayConfig() {
  _digit_buffer.displayType[0] = NORMAL;
  _digit_buffer.displayType[1] = NORMAL;
  _digit_buffer.displayType[2] = NORMAL;
  _digit_buffer.displayType[3] = NORMAL;
  _digit_buffer.displayType[4] = BLINK;
  _digit_buffer.displayType[5] = BLINK;
}

// ************************************************************
// Display preset
// ************************************************************
void OutputManager::allBlanked() {
  _digit_buffer.displayType[0] = BLANKED;
  _digit_buffer.displayType[1] = BLANKED;
  _digit_buffer.displayType[2] = BLANKED;
  _digit_buffer.displayType[3] = BLANKED;
  _digit_buffer.displayType[4] = BLANKED;
  _digit_buffer.displayType[5] = BLANKED;
}

// ************************************************************
// Display preset
// ************************************************************
void OutputManager::loadDisplaySetValueType() {
  _digit_buffer.displayType[5] = _value_buffer.valueDisplayType[5];
  _digit_buffer.displayType[4] = _value_buffer.valueDisplayType[4];
  _digit_buffer.displayType[3] = _value_buffer.valueDisplayType[3];
  _digit_buffer.displayType[2] = _value_buffer.valueDisplayType[2];
  _digit_buffer.displayType[1] = _value_buffer.valueDisplayType[1];
  _digit_buffer.displayType[0] = _value_buffer.valueDisplayType[0];
}

// ************************************************************
// Apply leading zero blanking
// ************************************************************
void OutputManager::applyBlanking() {

  // We only want to blank the hours tens digit
  if (cc->blankLeading && _digit_buffer.numberArray[0] == 0) {
    _digit_buffer.displayType[0] = BLANKED;
  }
  else {
    _digit_buffer.displayType[0] = NORMAL;
  }
}

// ************************************************************
// Set the time we are going to do the value display for
// ************************************************************
void OutputManager::setValueDisplayTime(int timeToDisplay) {
  _value_buffer.valueDisplayTime = timeToDisplay;
}

// ************************************************************
// Set the format to use for the value display
// ************************************************************
void OutputManager::setValueFormat(int newValueFormat) {
  for (int idx = 0 ; idx < DIGIT_COUNT ; idx++ ) {
    byte digitFormat = newValueFormat % 10;
    if (digitFormat > FORMAT_MAX) digitFormat = NORMAL;
    _value_buffer.valueDisplayType[DIGIT_COUNT - idx - 1] = digitFormat;
    newValueFormat = newValueFormat / 10;
  }
}

// ************************************************************
// Get the time we are going to do the value display for
// ************************************************************
int OutputManager::getValueDisplayTime() {
  return _value_buffer.valueDisplayTime;
}

// ************************************************************
// Decrement the time we are going to do the value display for
// ************************************************************
void OutputManager::decValueDisplayTime() {
  _value_buffer.valueDisplayTime--;
}

// ************************************************************
// Set the value to display
// ************************************************************
void OutputManager::setValueToShow(int newValue) {
  int maskVal = 10^DIGIT_COUNT;
  _value_buffer.valueToShow = newValue % maskVal;
}

// ************************************************************
// Get the digit at index idx
// ************************************************************
byte OutputManager::getNumberArrayIndexedValue(byte idx) {
  return _digit_buffer.numberArray[idx];
}

// ************************************************************
// Set the digit at index idx
// ************************************************************
void OutputManager::setNumberArrayIndexedValue(byte idx, byte value) {
  _digit_buffer.numberArray[idx] = value;
}

// ************************************************************
// Get the display type at index idx
// ************************************************************
byte OutputManager::getDisplayTypeIndexedValue(byte idx) {
  return _digit_buffer.displayType[idx];
}

// ************************************************************
// Set the display type at index idx
// ************************************************************
void OutputManager::setDisplayTypeIndexedValue(byte idx, byte value) {
  _digit_buffer.displayType[idx] = value;
}
