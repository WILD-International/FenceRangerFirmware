#include "at_commands.h"

#include "config.h"
#include "debug.h"
#include "helpers.h"

String serialBuffer;

void handleATCommand(String cmd, Config& config, bool& exitConfigMode);

void handleATSerial(char c, Config& config, bool& exitConfigMode) {
  if (c == '\n' || c == '\r') {
    if (serialBuffer.length() > 0) {
      handleATCommand(serialBuffer, config, exitConfigMode);
      serialBuffer = "";
    }
  } else {
    serialBuffer += c;
  }

  if (serialBuffer.length() > 128) {
    serialBuffer = "";
  }
}

void handleATCommand(String cmd, Config& config, bool& exitConfigMode) {
  String originalCmd = cmd;
  originalCmd.trim();

  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "AT") {
    Serial.println("OK");
  } else if (cmd == "AT+DEVEUI?") {
    char hex[DEVEUI_LEN * 2 + 1] = {};
    bytesToHex(config.getDevEUI(), DEVEUI_LEN, hex);
    Serial.printf("+DEVEUI:%s\r\nOK\r\n", hex);
  } else if (cmd == "AT+APPKEY?") {
    char hex[APPKEY_LEN * 2 + 1] = {};
    bytesToHex(config.getAppKey(), APPKEY_LEN, hex);
    Serial.printf("+APPKEY:%s\r\nOK\r\n", hex);
  } else if (cmd.startsWith("AT+APPKEY=")) {
    String val = cmd.substring(10);
    uint8_t tmp[APPKEY_LEN];
    if (val.length() == 0) {
      config.clearAppKey();
      Serial.println("OK");
    } else if (hexToBytes(val.c_str(), tmp, APPKEY_LEN)) {
      config.setAppKey(tmp);
      Serial.println("OK");
    } else {
      Serial.println("ERROR: Invalid AppKey (expected 32 hex chars)");
    }
  } else if (cmd == "AT+DEVNAME?") {
    Serial.printf("+DEVNAME:%s\r\nOK\r\n", config.getDevName());
  } else if (cmd.startsWith("AT+DEVNAME=")) {
    String val = originalCmd.substring(11);
    val.trim();
    if (val.length() == 0 || val.length() >= DEVNAME_MAX) {
      Serial.printf("ERROR: Name must be 1-%d chars\r\n", DEVNAME_MAX - 1);
    } else {
      config.setDevName(val.c_str());
      Serial.println("OK");
    }
  } else if (cmd == "AT+EXIT") {
    Serial.println("OK");
    Debug::enable();
    exitConfigMode = true;
  } else if (cmd == "AT+HELP") {
    Serial.println("AT          - Ping");
    Serial.println("AT+DEVEUI?  - Get DevEUI (read-only)");
    Serial.println("AT+APPKEY?  - Get AppKey");
    Serial.println("AT+APPKEY=  - Set AppKey (32 hex chars)");
    Serial.println("AT+DEVNAME? - Get device name");
    Serial.println("AT+DEVNAME= - Set device name");
    Serial.println("AT+EXIT     - Exit config mode");
    Serial.println("OK");
  } else {
    Serial.println("ERROR: Unknown command");
  }
}
