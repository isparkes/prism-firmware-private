#include "NtpAsync.h"

// ************************************************************
// Reset defaults and set up
// ************************************************************
void NtpAsync::setUp() {
  debugMsg("Set up");
  resetDefaults();
}

// ************************************************************
// reset all the internal defaults
// ************************************************************
void NtpAsync::resetDefaults() {
  setNtpPool(NTP_POOL_DEFAULT);
  setUpdateInterval(NTP_UPDATE_INTERVAL_DEFAULT);
  setTZS(TIME_ZONE_STRING_DEFAULT);
  debugMsg("Reset defaults");
}

// ************************************************************
// Set the time zone string
// ************************************************************
void NtpAsync::setTZS(String tzs) {
  _tzs = tzs;
  setenv("TZ", _tzs.c_str(), 1);

  // We changed the time zone, we need to force an update
  resetNextUpdate();
}

// ************************************************************
// get the time zone string
// ************************************************************
String NtpAsync::getTZS() {
  return _tzs;
}

// ************************************************************
// set the update interval
// ************************************************************
void NtpAsync::setUpdateInterval(int updateInterval) {
  _ntpUpdateInterval = updateInterval;
  
  // We changed the update interval, we need to force an update
  resetNextUpdate();
}

// ************************************************************
// get the update interval
// ************************************************************
int NtpAsync::getUpdateInterval() {
  return _ntpUpdateInterval;
}

// ************************************************************
// set the update interval
// ************************************************************
void NtpAsync::setNtpPool(String ntpPool) {
  _ntpPool = ntpPool;
}

// ************************************************************
// get the update interval
// ************************************************************
String NtpAsync::getNtpPool() {
  return _ntpPool;
}

// ************************************************************
// get the number of millis until the next update is due in seconds
// ************************************************************
signed int NtpAsync::getNextUpdate(unsigned long nowMillis) {
  checkMillisOverflow(nowMillis);
  
  // deal with the startup case - we always want to update 
  if (_lastUpdateFromServer == 0) {
    return -1;
  } else {
    return (_lastUpdateFromServer/1000 + _ntpUpdateInterval) - nowMillis/1000;
  }
}

// ************************************************************
// get the update interval
// ************************************************************
unsigned long NtpAsync::getLastUpdate() {
  return _lastUpdateFromServer;
}


// ************************************************************
// Invalidate the last update time and trigger a new NTP update
// ************************************************************
void NtpAsync::resetNextUpdate() {
  _lastUpdateFromServer = 0;
}

// ************************************************************
// get the last time we got back
// ************************************************************
String NtpAsync::getLastTimeFromServer() {
  return _lastTimeFromServer;
}

// ************************************************************
// see if the NTP we got is still to be condsidered valid
// ************************************************************
bool NtpAsync::ntpTimeValid(unsigned long nowMillis) {
  checkMillisOverflow(nowMillis);
  return _lastUpdateFromServer != 0 && ((nowMillis - _lastUpdateFromServer) < (2000 * _ntpUpdateInterval));
}

// ************************************************************
// get the number of seconds since the last update for display
// ************************************************************
long NtpAsync::getLastUpdateTimeSecs(unsigned long nowMillis) {
  return (nowMillis - _lastUpdateFromServer)/1000;
}

// ************************************************************
// set the update interval
// ************************************************************
void NtpAsync::setDebugOutput(bool newDebug) {
  _debug = newDebug;
}

// ************************************************************
// Asynchronous NTP query
// ************************************************************
void NtpAsync::getTimeFromNTP() {
  debugMsg("Async NTP in");

  if (WiFi.status() != WL_CONNECTED) {
    debugMsg("WiFi not connected. Abort.");
    return;
  }

  IPAddress serverIP;
  WiFi.hostByName(_ntpPool.c_str(), serverIP);
  String str = "NTP IPAddr: ";
  str += serverIP.toString();
  debugMsg(str);

  byte buffer[NTP_PACKET_SIZE];
  memset(buffer, 0, NTP_PACKET_SIZE);
  buffer[0] = 0b11100011;   // LI, Version, Mode
  buffer[1] = 0;            // Stratum, or type of clock
  buffer[2] = 9;            // Polling Interval (9 = 2^9 secs = ~9 mins, close to our 10 min default)
  buffer[3] = 0xEC;         // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  buffer[12]  = 'X';        // "kiss code", see RFC5905
  buffer[13]  = 'W';        // (codes starting with 'X' are not interpreted)
  buffer[14]  = 'N';
  buffer[15]  = 'C';

  _ntpStarted = millis();
  debugMsg("Connect to NTP");
  _udp.connect(serverIP, 123); //NTP requests are to port 123
  debugMsg("Write to NTP");
  _udp.write(buffer, NTP_PACKET_SIZE);
  debugMsg("NTP Packet sent at " + String(_ntpStarted));

  _udp.onPacket([&](AsyncUDPPacket packet) {
    String message = "NTP response UDP Packet Type: " + packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast" : "Unicast";
    message += " from " + packet.remoteIP().toString() + ":" + String(packet.remotePort());
    message += " to " + packet.localIP().toString() + ":" + String(packet.localPort());
    debugMsg(message);

    if (packet.length() != 48) {
      debugMsg("Received data, but got invalid length: " + String(packet.length()));
    } else {
      uint8_t buffer[NTP_PACKET_SIZE];
      memcpy(&buffer, packet.data(), packet.length());
      //prepare timestamps
      uint32_t highWord, lowWord;
      highWord = ( buffer[16] << 8 | buffer[17] ) & 0x0000FFFF;
      lowWord = ( buffer[18] << 8 | buffer[19] ) & 0x0000FFFF;
      uint32_t reftsSec = highWord << 16 | lowWord;       // reference timestamp seconds

      highWord = ( buffer[32] << 8 | buffer[33] ) & 0x0000FFFF;
      lowWord = ( buffer[34] << 8 | buffer[35] ) & 0x0000FFFF;
      uint32_t rcvtsSec = highWord << 16 | lowWord;       // receive timestamp seconds

      highWord = ( buffer[40] << 8 | buffer[41] ) & 0x0000FFFF;
      lowWord = ( buffer[42] << 8 | buffer[43] ) & 0x0000FFFF;
      uint32_t secsSince1900 = highWord << 16 | lowWord;      // transmit timestamp seconds

      highWord = ( buffer[44] << 8 | buffer[45] ) & 0x0000FFFF;
      lowWord = ( buffer[46] << 8 | buffer[47] ) & 0x0000FFFF;
      uint32_t fraction = highWord << 16 | lowWord;       // transmit timestamp fractions

      //check if received data makes sense
      //buffer[1] = stratum - should be 1..15 for valid reply
      //also checking that all timestamps are non-zero and receive timestamp seconds are <= transmit timestamp seconds
      if ((buffer[1] < 1) or (buffer[1] > 15) or (reftsSec == 0) or (rcvtsSec == 0) or (rcvtsSec > secsSince1900)) {
        // we got invalid packet
        debugMsg("Got data, but got INVALID_DATA");
        return;
      }

      // Set the t and measured_at variables that were passed by reference
      unsigned long done = millis();
      debugMsg("success round trip " + String(done - _ntpStarted) + " ms");
      unsigned long t = secsSince1900 - 2208988800UL;                     // Subtract 70 years to get seconds since 1970
      uint16_t ms = fraction / 4294967UL;                                 // Turn 32 bit fraction into ms by dividing by 2^32 / 1000
      unsigned long measured_at = done - ((done - _ntpStarted) / 2) - ms;  // Assume symmetric network latency and return when we think the whole second was.

      _lastUpdateFromServer = done;

      debugMsg("_lastUpdateFromServer: " + String(_lastUpdateFromServer));
      
      time_t ntpTime = t;
      const tm* tm = localtime(&ntpTime);

      String timeString = String(tm->tm_year + 1900) + "," + String(tm->tm_mon + 1) + "," + String(tm->tm_mday) + "," + String(tm->tm_hour) + "," + String(tm->tm_min) + "," + String(tm->tm_sec);
      debugMsg("NTP Update time str: " + timeString);
      _lastTimeFromServer = timeString;

      // Notify the outside world that we have updated
      if (_ntcb != NULL) {
        _ntcb();
      }
    }
  });

  debugMsg("Async GET Time out");
}

// ************************************************************
// Output a logging message to the debug output, if set
// ************************************************************
void NtpAsync::debugMsg(String message) {
  if (_dbcb != NULL && _debug) {
    _dbcb("NTP: " + message);
  }
}

// ************************************************************
// Set the callback for outputting debug messages
// ************************************************************
void NtpAsync::setDebugCallback(DebugCallback dbcb) {
  _dbcb = dbcb;
  debugMsg("Debugging started, callback set");
}

// ************************************************************
// Set the callback for informing that a new time update is there
// ************************************************************
void NtpAsync::setNewTimeCallback(NewTimeCallback ntcb) {
  _ntcb = ntcb;
  debugMsg("Time update callback set");
}

// ************************************************************
// Check if we have had a time overflow and reset if so
// ************************************************************
void NtpAsync::checkMillisOverflow(unsigned long nowMillis) {
  if (nowMillis < _lastUpdateFromServer) {
    // Overflow, reset
    resetNextUpdate();
  }
}
