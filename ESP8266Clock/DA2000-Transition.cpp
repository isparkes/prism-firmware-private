/**
 * A class that displays a message by wiping it into and out of the display
 *
 * Thanks Judge & Ty!
 */

#include "Arduino.h"
#include "DA2000-Transition.h"

Transition::Transition(int effectInDuration, int effectOutDuration, int holdDuration, int selectedEffect) {
  _effectInDuration = effectInDuration;
  _effectOutDuration = effectOutDuration;
  _holdDuration = holdDuration;
  _selectedEffect = selectedEffect;
  _started = 0;
  _end = 0;
}

void Transition::start(unsigned long now) {
  if (_end < now) {
    // save the target display
    OutputManager::Instance().loadNumberArrayDate();
    for (int idx = 0; idx < DIGIT_COUNT ; idx++) {
      _alternateDisplay[idx] = OutputManager::Instance().getNumberArrayIndexedValue(idx);
    }

    // save the current version of the normal display
    OutputManager::Instance().loadNumberArrayTime();
    for (int idx = 0; idx < DIGIT_COUNT ; idx++) {
      _regularDisplay[idx] = OutputManager::Instance().getNumberArrayIndexedValue(idx);
      _savedDisplayType[idx] = OutputManager::Instance().getDisplayTypeIndexedValue(idx);
    }
    _started = now;
    _end = getEnd();
  }
  // else we are already running!
}

boolean Transition::runEffect(unsigned long now, boolean blankLeading) {
  switch (_selectedEffect) {
    case SLOTS_MODE_WIPE_WIPE:
      return wipeInWipeOut(now, blankLeading);
      break;
    case SLOTS_MODE_BANG_BANG:
      return bangInBangOut(now);
      break;
    default:
      return false;
      break;
  }
}

boolean Transition::isMessageOnDisplay(unsigned long now)
{
  return (now < _end);
}

boolean Transition::wipeInWipeOut(unsigned long now, boolean blankLeading)
{
  if (now < _end) {
    int msCount = now - _started;
    // Wipe In blanking
    if (msCount < _effectInDuration) {
      _digit = msCount * (DIGIT_COUNT + 1) / _effectInDuration;
      if (_digit > 0)
        OutputManager::Instance().setDisplayTypeIndexedValue(_digit-1, BLANKED);
    }
    // Wipe In date values
    else if (msCount < _effectInDuration * 2) {
      _digit = (msCount - _effectInDuration) * DIGIT_COUNT / _effectInDuration;
      OutputManager::Instance().setNumberArrayIndexedValue(_digit, _alternateDisplay[_digit]);
      OutputManager::Instance().setDisplayTypeIndexedValue(_digit, NORMAL);
    }
    // Hold date display
    else if (msCount < _effectInDuration * 2 + _holdDuration) {
      OutputManager::Instance().loadNumberArrayDate();
    }
    // Wipe Out blanking
    else if (msCount < _effectInDuration * 2 + _holdDuration + _effectOutDuration) {
      _digit = (msCount - _holdDuration - _effectInDuration * 2) * DIGIT_COUNT / _effectOutDuration;
      OutputManager::Instance().setDisplayTypeIndexedValue(_digit, BLANKED);
    }
    // Wipe Out to time values
    else if (msCount < _effectInDuration * 2 + _holdDuration + _effectOutDuration * 2) {
      _digit = (msCount - _holdDuration - _effectInDuration * 2 - _effectOutDuration) * DIGIT_COUNT / _effectOutDuration;
      OutputManager::Instance().setNumberArrayIndexedValue(_digit, _regularDisplay[_digit]);
      if (!blankLeading || _digit != 0 || _regularDisplay[_digit] != 0)
        OutputManager::Instance().setDisplayTypeIndexedValue(_digit, NORMAL);
    }
    // We now return you to your regularly scheduled program
    else {
      OutputManager::Instance().loadNumberArrayTime();
      OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
      _end = 0;
   }
    return true;  // we are still running
  }
  return false;   // We aren't running
}

boolean Transition::bangInBangOut(unsigned long now)
{
  if (now < _end) {
    int msCount = now - _started;
    // Bang In blanking
    if (msCount < _effectInDuration) {
      OutputManager::Instance().allBlanked();
    }
    // Bang In date values
    else if (msCount < _effectInDuration * 2) {
      OutputManager::Instance().loadNumberArrayDate();
      OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);
    }
    // Hold date display
    else if (msCount < _effectInDuration * 2 + _holdDuration) {
      OutputManager::Instance().loadNumberArrayDate();
      OutputManager::Instance().allNormal(DO_NOT_APPLY_LEAD_0_BLANK);
    }
    // Bang Out blanking
    else if (msCount < _effectInDuration * 2 + _holdDuration + _effectOutDuration) {
      OutputManager::Instance().allBlanked();
    }
    // Bang Out to time values
    else if (msCount < _effectInDuration * 2 + _holdDuration + _effectOutDuration * 2) {
      OutputManager::Instance().loadNumberArrayTime();
      OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
    }
    // We now return you to your regularly scheduled program
    else {
      OutputManager::Instance().loadNumberArrayTime();
      OutputManager::Instance().allNormal(APPLY_LEAD_0_BLANK);
      _end = 0;
   }
    return true;  // we are still running
  }
  return false;   // We aren't running
}

unsigned long Transition::getEnd() {
  return _started + _effectInDuration * 2 + _holdDuration + _effectOutDuration * 2;
}

// Update the seconds in the internal buffer only needed with 6 digit displays
void Transition::updateRegularDisplaySeconds(byte secondUpdate) {
  if (DIGIT_COUNT == 6) {
    _regularDisplay[4] = secondUpdate / 10;
    _regularDisplay[5] = secondUpdate % 10;
  }
}
