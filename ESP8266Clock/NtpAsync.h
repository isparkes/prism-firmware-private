#ifndef NtpAsync_h
#define NtpAsync_h

#include <memory>
#include <Arduino.h>
#include <ESPAsyncUDP.h>        // https://github.com/me-no-dev/ESPAsyncUDP (master HEAD)
//#include <AsyncUDP.h>         
#include <ESP8266WiFi.h>
#include <DNSServer.h>          //https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer

// ------------------------ Types ------------------------

typedef void (*DebugCallback) (String);
typedef void (*NewTimeCallback) ();

// ----------------------- Defines -----------------------

#define NTP_POOL_DEFAULT "pool.ntp.org"
#define NTP_PACKET_SIZE 48
#define TIME_ZONE_STRING_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_UPDATE_INTERVAL_DEFAULT 7261
#define NTP_UPDATE_INTERVAL_MIN 60
#define NTP_UPDATE_INTERVAL_MAX 86400

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------ NTP Component -------------------------------------------
// ----------------------------------------------------------------------------------------------------

class NtpAsync
{
  public:
    void setUp();
    void setDebugOutput(bool newDebug);
    void getTimeFromNTP();
    bool getIsConnected();
    void resetDefaults();
    
    void setTZS(String newTzs);
    String getTZS();
    
    void setUpdateInterval(int updateInterval);
    int getUpdateInterval();
    
    String getNtpPool();
    void setNtpPool(String pool);
    
    unsigned long getLastUpdate();
    signed int getNextUpdate(unsigned long nowMillis);
    void resetNextUpdate();
    
    String getLastTimeFromServer();
    
    bool ntpTimeValid(unsigned long nowMillis);
    
    long getLastUpdateTimeSecs(unsigned long nowMillis);

    // callbacks
    void setDebugCallback(DebugCallback dbcb);
    void setNewTimeCallback(NewTimeCallback ntcb);
  private:
    String _ntpPool = NTP_POOL_DEFAULT;                   // The pool name we are using
    String _tzs = TIME_ZONE_STRING_DEFAULT;               // The TZ value to use
    unsigned long _lastUpdateFromServer = 0;              // The last millis() we got an update at
    String _lastTimeFromServer = "";                      // The last time we got
    int _ntpUpdateInterval = NTP_UPDATE_INTERVAL_DEFAULT; // The interval between updates in SECONDS
    unsigned long _ntpStarted = 0;
    bool _debug = false;
    AsyncUDP _udp;
    DebugCallback _dbcb;
    NewTimeCallback _ntcb;

    void debugMsg(String message);                        // print a debug message to the callback
    void checkMillisOverflow(unsigned long nowMillis);    // handle millis overflow
};

// ----------------- Exported Variables ------------------

static NtpAsync ntpAsync;

#endif
