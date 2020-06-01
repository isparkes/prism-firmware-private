#ifndef __HMTL_SERVER_H__
#define __HMTL_SERVER_H__

#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include "ClockUtils.h"

// ----------------------- Auth ------------------------

#define WEB_USERNAME_DEFAULT "admin"
#define WEB_PASSWORD_DEFAULT "setup"
#define WEB_AUTH_DEFAULT true

// -----------------------------------------------------

#define HTML_TITLE "ESP 6-Digit Nixie Clock"

extern ESP8266WebServer server;
extern ESP8266HTTPUpdateServer httpUpdater;
extern MDNSResponder mdns;

boolean getWebAuthentication();
void setWebAuthentication(boolean newValue);
String getWebUserName();
void setWebUserName(String newValue);
String getWebPassword();
void setWebPassword(String newValue);

void setServerPageHandlers();
void checkServerArgBoolean(String argument, String argumentName, String trueLiteral, String falseLiteral, boolean &changed, boolean &value);
void checkServerArgInt(String argument, String argumentName, boolean &changed, int &value);
void checkServerArgByte(String argument, String argumentName, boolean &changed, byte &value);
void checkServerArgString(String argument, String argumentName, boolean &changed, String &value);

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- HTML functions -----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getHTMLHead(boolean isConnected);
String getNavBar();
String getExplanationText(String textToprint);
String getTableHead2Col(String tableHeader, String col1Header, String col2Header);
String getTableRow2Col(String col1Val, String col2Val);
String getTableRow2Col(String col1Val, int col2Val);
String getTableFoot();
String getFormHead(String formTitle);
String getFormFoot();
String getHTMLFoot();
String getRadioGroupHeader(String header);
String getRadioButton(String group_name, String text, String value, boolean checked);
String getRadioGroupFooter();
String getCheckBox(String checkbox_name, String value, String text, boolean checked);
String getDropDownHeader(String heading, String group_name, boolean wide, boolean disabled);
String getDropDownOption (String value, String text, boolean checked);
String getDropDownFooter();
String getNumberInput(String heading, String input_name, unsigned int minVal, unsigned int maxVal, unsigned int value, boolean disabled);
String getNumberInputWide(String heading, String input_name, byte minVal, byte maxVal, byte value, boolean disabled);
String getTextInput(String heading, String input_name, String value, boolean disabled);
String getTextInputWide(String heading, String input_name, String value, boolean disabled);
String getSubmitButton(String buttonText);
String getHTMLFoot();

#endif
