#ifndef ledmanager_h
#define ledmanager_h

#include "Arduino.h"
#include <NeoPixelBus.h>        // https://github.com/Makuna/NeoPixelBus (Makuna 2.3.4)
#include "SPIFFS.h"
#include "OutputManagerMicrochip6.h"

// --------------------------- Strategy Backlights -------------------------------
#define BACKLIGHT_MIN                   0
#define BACKLIGHT_FIXED                 0  // Just define a colour and stick to it
#define BACKLIGHT_CYCLE                 1  // cycle through random colours, strategy 3
#define BACKLIGHT_COLOUR_TIME           2  // use "ColourTime" - different colours for each digit value
#define BACKLIGHT_DAY_OF_WEEK           3  // use "ColourTime" - different colours for each digit value
#define BACKLIGHT_MAX                   3
#define BACKLIGHT_DEFAULT               1

// -------------------------------------------------------------------------------
#define CYCLE_SPEED_MIN                 4
#define CYCLE_SPEED_MAX                 64
#define CYCLE_SPEED_DEFAULT             10

// -------------------------------------------------------------------------------
#define COLOUR_CNL_MAX                  15
#define COLOUR_RED_CNL_DEFAULT          15
#define COLOUR_GRN_CNL_DEFAULT          0
#define COLOUR_BLU_CNL_DEFAULT          0
#define COLOUR_CNL_MIN                  0

// -------------------------------------------------------------------------------
#define STATUS_RED    0
#define STATUS_YELLOW 1
#define STATUS_GREEN  2
#define STATUS_BLUE   3

// ************************** Pin Allocations *************************
#define LED_DOUT                        3     // RX defined by DMA output of library

class LEDManager
{
  public:
    void setUp();

    // reset internal values based on slow-moving values (e.g. backlight dim factor)
    void recalculateVariables();

    // recalculate internal values based on the LDR reading
    void setLDRValue(unsigned int ldrValue);

    // recalculate internal values based on the pulsing factor
    void setPulseValue(unsigned int secsDelta);

    // Change LED blanking status
    void setBlanked(boolean blanked);

    void setSyncColourTime(boolean value);
    void setDiagnosticLED(byte stepNumber, byte state);

    // recalculate internal values based on the LDR reading
    void setDayOfWeek(byte dow);

    // This processes the values and outputs the buffer
    void processLedStatus();

  private:
    float _backlightDim = 1.0;
    float _underlightDim = 1.0;
    float _ldrDimFactor = 1.0;
    float _pwmFactor = 1.0;
    boolean _blanked = false;
    byte _ledMode = BACKLIGHT_DEFAULT;
    byte _cycleCount = 0;
    byte _cycleSpeed = CYCLE_SPEED_DEFAULT;
    boolean _syncColourTime = false;
    byte _dow = 0;
    spiffs_config_t *cc = &current_config;

    // Strategy 3
    int changeSteps = 0;
    byte currentColour = 0;
    
    int colors[3];
    byte cycleCount = 0;

    // Back lights
    byte ledRb[DIGIT_COUNT];
    byte ledGb[DIGIT_COUNT];
    byte ledBb[DIGIT_COUNT];

    // Under lights
    byte ledRu[DIGIT_COUNT];
    byte ledGu[DIGIT_COUNT];
    byte ledBu[DIGIT_COUNT];

    void setBacklightLEDs(byte red, byte green, byte blue);
    void setUnderlightLEDs(byte red, byte green, byte blue);
    void outputLEDBuffer();
    byte getLEDAdjustedBL(byte rawValue);
    byte getLEDAdjustedUL(byte rawValue);
    void cycleColours3(int colors[3]);
};

// ************************************************************
// LED brightness correction: The perceived brightness is not linear
// ************************************************************
const byte dim_curve[] = {
  0,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,
  3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   6,   6,   6,
  6,   6,   6,   6,   6,   7,   7,   7,   7,   7,   7,   7,   8,   8,   8,   8,
  8,   8,   9,   9,   9,   9,   9,   9,   10,  10,  10,  10,  10,  11,  11,  11,
  11,  11,  12,  12,  12,  12,  12,  13,  13,  13,  13,  14,  14,  14,  14,  15,
  15,  15,  16,  16,  16,  16,  17,  17,  17,  18,  18,  18,  19,  19,  19,  20,
  20,  20,  21,  21,  22,  22,  22,  23,  23,  24,  24,  25,  25,  25,  26,  26,
  27,  27,  28,  28,  29,  29,  30,  30,  31,  32,  32,  33,  33,  34,  35,  35,
  36,  36,  37,  38,  38,  39,  40,  40,  41,  42,  43,  43,  44,  45,  46,  47,
  48,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
  63,  64,  65,  66,  68,  69,  70,  71,  73,  74,  75,  76,  78,  79,  81,  82,
  83,  85,  86,  88,  90,  91,  93,  94,  96,  98,  99,  101, 103, 105, 107, 109,
  110, 112, 114, 116, 118, 121, 123, 125, 127, 129, 132, 134, 136, 139, 141, 144,
  146, 149, 151, 154, 157, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 190,
  193, 196, 200, 203, 207, 211, 214, 218, 222, 226, 230, 234, 238, 242, 248, 255,
};

const byte rgb_backlight_curve[] = {0, 16, 32, 48, 64, 80, 99, 112, 128, 144, 160, 176, 192, 216, 240, 255};

// Used to define "colourTime" colours
//                             0    1    2    3    4    5    6    7    8    9
const byte colourTimeR[] = { 255, 255, 204,  51,   0,   0,   0, 153, 204, 255};
const byte colourTimeG[] = {   0, 153, 192, 192, 255, 192, 102,   0,   0,   0};
const byte colourTimeB[] = {   0,   0,   0,   0,  51, 192, 255, 255, 255, 153};

// Used to define "dayOfWeek" colours
//                            0    1    2    3    4    5    6
const byte dayOfWeekR[] = { 255, 255, 204,  51,   0,   0,   0};
const byte dayOfWeekG[] = {   0,   0,   0,   0,  51, 153, 255};
const byte dayOfWeekB[] = {   0, 153, 192, 255, 255, 153,   0};

// These are the "pure" HSV values turned to RGB
// Number                        0    1    2    3    4    5    6    7    8    9
// Degrees                       0   36   72  108  144  180  216  252  288  324
//const byte colourTimeR[] = { 255, 255, 204,  51,   0,   0,   0,  51, 204, 255};
//const byte colourTimeG[] = {   0, 153, 255, 255, 255, 255, 102,   0,   0,   0};
//const byte colourTimeB[] = {   0,   0,   0,   0, 102, 255, 255, 255, 255, 153};
// With these values 2/3 and 6/7 are not easily distinguishable
// 2 is too green, 6 is too blue


// ----------------- Exported Variables ------------------

static LEDManager ledManager;

#endif
