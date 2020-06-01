//**********************************************************************************
//* Utilities which can be separated from the main code                            *
//**********************************************************************************

#ifndef ClockUtils_h
#define ClockUtils_h

#include <FS.h>
#include <Wire.h>
// #include <ESP8266HTTPClient.h>
#include "HtmlServer.h"
#include "ClockDefs.h"

/*
 * A collection of utility functions
 */

String getBool(bool value);
String formatIPAsString(IPAddress ip);
int getIntValue(String data, char separator, int index);
String getValue(String data, char separator, int index);
void grabInts(String s, int *dest, String sep);
unsigned char hex2bcd (unsigned char x);
String secsToReadableString(long secsValue);

// ----------------------------------------------------------------------------------------------------
// --------------------------------------------- Debugging --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void setupDebug(boolean setUseDebug);
void debugMsg(String message);
void debugMsgCont(String message);
boolean getDebug();
void toggleDebug();

void hexCharacterStringToBytes(byte *byteArray, const char *hexString);
byte nibble(char c);

#endif
