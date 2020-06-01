#ifndef Transition_h
#define Transition_h

#include "Arduino.h"
#include "DisplayDefs.h"
#include "ClockDefs.h"
#include "OutputManagerMicrochip6.h"

class Transition
{
  public:
    Transition(int, int, int, int);
    void start(unsigned long);
    boolean isMessageOnDisplay(unsigned long);
    boolean runEffect(unsigned long, boolean blankLeading);
    void updateRegularDisplaySeconds(byte secondUpdate);
    
  private:
    int _effectInDuration;
    int _effectOutDuration;
    int _holdDuration;
    int _selectedEffect;
    int _digit;
    unsigned long _started;
    unsigned long _end;
    byte _regularDisplay[DIGIT_COUNT] = {0, 0, 0, 0, 0, 0};
    byte _alternateDisplay[DIGIT_COUNT] = {0, 0, 0, 0, 0, 0};
    byte _savedDisplayType[DIGIT_COUNT] = {0, 0, 0, 0, 0, 0};
    
    boolean wipeInWipeOut(unsigned long, boolean blankLeading);
    boolean bangInBangOut(unsigned long);
    unsigned long getEnd();
};

#endif
