#ifndef __ESP_DS1307_H__
#define __ESP_DS1307_H__

#include <Arduino.h>

// ----------------------- Defines -----------------------

#define DS1307_I2C_ADDRESS 0x68

#define MON 1
#define TUE 2
#define WED 3
#define THU 4
#define FRI 5
#define SAT 6
#define SUN 7

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- RTC Component ------------------------------------------
// ----------------------------------------------------------------------------------------------------

class DS1307 {
public:
    void begin();
    void startClock(void);
    void stopClock(void);
    void setTime(void);
    void getTime(void);
    void fillByHMS(uint8_t _hour, uint8_t _minute, uint8_t _second);
    void fillByYMD(uint8_t _year, uint8_t _month, uint8_t _day);
    void fillDayOfWeek(uint8_t _dow);
    uint8_t isRunning();
    uint8_t second;
    uint8_t minute;
    uint8_t hour; 
    uint8_t dayOfWeek;// day of week, 1 = Monday
    uint8_t dayOfMonth;
    uint8_t month;
    uint16_t year;
private:
    uint8_t decToBcd(uint8_t val);
    uint8_t bcdToDec(uint8_t val);
};

// ----------------- Exported Variables ------------------

static DS1307 Clock;   // RTC, uses Analogue pins A4 (SDA) and A5 (SCL)

#endif
