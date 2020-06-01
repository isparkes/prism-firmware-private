#include "DebugManager.h"

void DebugManager::setUp(bool newDebug) {
  if (!_debug && newDebug) {
    Serial.begin(115200);
    debugMsg("");
    debugMsg("--------------------- START ---------------------");
  }
  
  if (_debug && !newDebug) {
    debugMsg("");
    debugMsg("---------------------- END ----------------------");
    Serial.end();
  }
  
  _debug = newDebug;
}

void DebugManager::debugMsg(String message) {
  if (_debug) {
    Serial.println(message);
    Serial.flush();
  }
}

bool DebugManager::getDebug() {
  return _debug;
}
