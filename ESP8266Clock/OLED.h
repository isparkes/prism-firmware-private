#ifndef oled_h
#define oled_h

#include <memory>
#include <Adafruit_GFX.h>       // v1.6.1
#include <Adafruit_SSD1306.h>   // v2.0.1

// ----------------------- Defines -----------------------

#define PIR_NOT_INSTALLED 0
#define PIR_NO_MOVEMENT   1
#define PIR_MOVEMENT      2

#define STATUS_LINE_Y 54
#define WIFI_IND_X     6
#define NTP_IND_X     16
#define PIR_IND_X     26
#define BLANK_IND_X   38
#define TIME_IND_X    50
#define AM_IND_X     110

#define STATUS_BOX_X   0
#define STATUS_BOX_Y  52
#define STATUS_BOX_W 127
#define STATUS_BOX_H  12

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------ OLED Component ------------------------------------------
// ----------------------------------------------------------------------------------------------------

class OLED
{
  public:
    void setUp();
    void showStatusLine();
    void setWiFiStatus(bool newStatus);
    void setNTPStatus(bool newStatus);
    void setPIRStatus(byte newStatus);
    void setBlankStatus(bool newStatus);
    void setAMStatus(bool newStatus);
    void clearDisplay();
    void outputDisplay();
    void showScrollingMessage(String messageText);
    void setTimeString(String timeText);
  private:
    bool wifiStatus = false;
    bool ntpStatus = false;
    byte pirStatus = PIR_NOT_INSTALLED;
    bool blankStatus = false;
    bool ampm = false;
    String timeText = "xx:xx:xx";
    String bufferLines[6] = {"","","","","",""};
    byte bufferIdx = 0;
    std::unique_ptr<Adafruit_SSD1306> _display;

    void drawWiFiInd();
    void drawNTPInd();
    void drawPIRInd();
    void drawBlankInd();
    void drawAMInd();
    void drawTimeInd();
};

// ----------------- Exported Variables ------------------

static OLED oled;

#endif
