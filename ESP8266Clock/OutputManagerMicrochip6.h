#ifndef outputmanager_h
#define outputmanager_h

#include "Arduino.h"
#include "DisplayDefs.h"
#include "SPIFFS.h"
#include "LEDManager.h"

#define DIGIT_COUNT            6

#define COUNTS_PER_DIGIT       20
#define COUNTS_PER_DIGIT_DIM   8

extern boolean led1State;
extern boolean led2State;
extern boolean ledLState;
extern boolean ledRState;
extern boolean blankTubes;
extern volatile uint32_t valueBufferCurr1[COUNTS_PER_DIGIT];
extern volatile uint32_t valueBufferCurr2[COUNTS_PER_DIGIT];

#define DIM_VALUE             DIGIT_DISPLAY_COUNT / 5;

// Display mode, set per digit
#define BLANKED  0
#define DIMMED   1
#define NORMAL   2
#define FADE     3
#define SCROLL   4
#define BLINK    5
#define BRIGHT   6
#define FORMAT_MAX   BRIGHT

#define BLINK_COUNT_ON        70                      // The number of impressions blinking digits are ON
#define BLINK_COUNT_OFF       55                      // The number of impressions blinking digits are OFF

#define SEP_DIM_MIN           1
#define SEP_BRIGHT            1
#define SEP_DIM               2
#define SEP_DIM_MAX           2
#define SEP_DIM_DEFAULT       SEP_DIM

#define DRIVER_HV5622
#ifdef DRIVER_HV5530
const unsigned int DECODE_DIGIT[] = { 0x0001, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002};
const unsigned int DECODE_LED[]   = { 0x0001, 0x0200, 0x0100, 0x0080};
#endif

#ifdef DRIVER_HV5622
const unsigned int DECODE_DIGIT[] = { 0x0200, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100};
const unsigned int DECODE_LED[]   = { 0x40000000, 0x80000000};
#endif

// ************************** Pin Allocations *************************

// Shift register
#define LATCHPin               14    // D5
#define CLOCKPin               12    // D6
#define DATAPin                13    // D7
#define BLANKPin               16    // D0

// -------------------------------------------------------------------------------
// How quickly the scroll works
// 0 means "off"
#define SCROLL_STEPS_DEFAULT 0
#define SCROLL_STEPS_MIN     1
#define SCROLL_STEPS_MAX     80

// -------------------------------------------------------------------------------
// The number of dispay impessions we need to fade by default
// 100 is about 1 second
// 0 means "off"
#define FADE_STEPS_DEFAULT 50
#define FADE_STEPS_MIN     20
#define FADE_STEPS_MAX     200

// ************************* Shared Structures ************************

typedef struct {
    byte numberArray[DIGIT_COUNT];
    byte currentNumberArray[DIGIT_COUNT];
    byte displayType[DIGIT_COUNT];
    byte fadeState[DIGIT_COUNT];
    boolean digitBlanked[DIGIT_COUNT];
} digit_buffer_t;

typedef struct {
  int valueToShow;
  byte valueDisplayTime;
  byte valueDisplayType[DIGIT_COUNT];
} value_buffer_t;

// ************************ Display management ************************
class OutputManager {
  public:
    static void CreateInstance();
    static OutputManager& Instance();

    void setUp();
    void setConfigObject(spiffs_config_t* ccPtr);
    void outputDisplay();
    void outputDisplayDiags();
    void setLDRValue(unsigned int newBrightness);

    void loadNumberArrayTime();
    void loadNumberArrayDate();
    void loadNumberArraySameValue(byte val);
    void loadNumberArrayPOSTMessage(int val);
    void loadNumberArrayTestDigits();
    void loadNumberArrayConfInt(int confValue, int confNum);
    void loadNumberArrayConfIntWide(int confValue);
    void loadNumberArrayConfBool(boolean confValue, int confNum);
    void loadNumberArrayIP(byte byte1, byte byte2);
    void loadNumberArrayLDR();
    void loadNumberArrayESPID(String ID);
    void loadNumberArrayValueToShow();
    void loadDisplaySetValueType();

    void allNormal(bool leadingBlank);
    void highlight0and1();
    void highlight2and3();
    void highlight4and5();
    void highlightDaysDateFormat();
    void highlightMonthsDateFormat();
    void highlightYearsDateFormat();
    void displayConfig();
    void allBlanked();

    // Arbitrary value
    void setValueDisplayTime(int timeToDisplay);
    int  getValueDisplayTime();
    void decValueDisplayTime();
    void setValueToShow(int newValue);
    void setValueFormat(int newValueFormat);

    // used for transition stunts
    byte getNumberArrayIndexedValue(byte idx);
    void setNumberArrayIndexedValue(byte idx, byte value);
    byte getDisplayTypeIndexedValue(byte idx);
    void setDisplayTypeIndexedValue(byte idx, byte value);
  private:
    static OutputManager* pInstance;

    int   _blinkCounter;
    boolean _blinkState;
    byte _preheatCounter = 0;
    int _ldrValue = 0;
    int _lastLDRValue = 0;
    int _tubeLag = 0;
    byte _separatorDim = 0;

    float _fadeStep = 0;

    spiffs_config_t *cc;

    digit_buffer_t _digit_buffer = {{0,0,0,0,0,0}, {0,0,0,0,0,0}, {NORMAL,NORMAL,NORMAL,NORMAL,NORMAL,NORMAL}, {0,0,0,0,0,0},{false, false, false, false, false, false} };
    value_buffer_t _value_buffer = {0,10,{NORMAL,NORMAL,NORMAL,NORMAL,NORMAL,NORMAL}};
    void setDigitBuffers(byte digit, byte value, byte currValue, byte dimFactor, byte switchTime, bool blanked);
    void blankDigits(byte *digits, byte *prevNums, bool *smoothRun);
    void smoothDigits(byte *digits, byte *prevNums, bool *smoothRun);
    void setBlankingPin();
    void applyBlanking();
    int getSwitchTime(byte offCount, byte fadeState, byte fadeSteps);
};

#endif
