#include "at_commands.h"

#include "config.h"
#include "debug.h"
#include "helpers.h"
#include "state.h"

String serialBuffer;
uint8_t serial_enable_at_mode_count = 0;

// Private method
void handleATCommand(String cmd, Config& config, State& state);

void handleATSerial(char c, Config& config, State& state) {
  // This function is called for each incoming character on Serial.
  // We will build up a command string until we see a newline, then call handleATCommand.
  // The main loop will also watch for "+++" to enter AT mode.
  if (c == '+') {
    serial_enable_at_mode_count++;
  } else {
    serial_enable_at_mode_count = 0;
  }
  if (!state.atCommandMode && serial_enable_at_mode_count >= 3) {
    state.atCommandMode = true;
    Debug::disable();
    Serial.println("");
    serialBuffer = "";
    handleATCommand("AT+HELP", config, state);
  } else if (state.atCommandMode) {
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        handleATCommand(serialBuffer, config, state);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }

  if (serialBuffer.length() > 128) {
    serialBuffer = "";  // prevent overflow
  }
}

void handleATCommand(String cmd, Config& config, State& state) {
  String originalCmd = cmd;
  originalCmd.trim();

  // use uppercase version for matching
  cmd.trim();
  cmd.toUpperCase();

  // AT (ping)
  if (cmd == "AT") {
    Serial.println("OK");
  }
  // AT+DEVEUI?
  else if (cmd == "AT+DEVEUI?") {
    char hex[DEVEUI_LEN * 2 + 1] = {};
    bytesToHex(config.getDevEUI(), DEVEUI_LEN, hex);
    Serial.printf("+DEVEUI:%s\r\nOK\r\n", hex);
  }
  // AT+APPKEY?
  else if (cmd == "AT+APPKEY?") {
    char hex[APPKEY_LEN * 2 + 1] = {};
    bytesToHex(config.getAppKey(), APPKEY_LEN, hex);
    Serial.printf("+APPKEY:%s\r\nOK\r\n", hex);
  }
  // AT+APPKEY=<32 hex chars>
  else if (cmd.startsWith("AT+APPKEY=")) {
    String val = cmd.substring(10);
    uint8_t tmp[APPKEY_LEN];
    // If no value is set, clear the AppKey
    if (val.length() == 0) {
      config.clearAppKey();
      Serial.println("OK");
    } else if (hexToBytes(val.c_str(), tmp, APPKEY_LEN)) {
      config.setAppKey(tmp);
      Serial.println("OK");
    } else {
      Serial.println("ERROR: Invalid AppKey (expected 32 hex chars)");
    }
  }
  // AT+DEVNAME?
  else if (cmd == "AT+DEVNAME?") {
    Serial.printf("+DEVNAME:%s\r\nOK\r\n", config.getDevName());
  }
  // AT+DEVNAME=<name>
  else if (cmd.startsWith("AT+DEVNAME=")) {
    String val = originalCmd.substring(11);
    val.trim();
    if (val.length() == 0 || val.length() >= DEVNAME_MAX) {
      Serial.printf("ERROR: Name must be 1-%d chars\r\n", DEVNAME_MAX - 1);
    } else {
      config.setDevName(val.c_str());
      Serial.println("OK");
    }
  }
  // AT+EXIT
  else if (cmd == "AT+EXIT") {
    Serial.println("OK");
    state.atCommandMode = false;
    Debug::enable();
    state.setMode(ModeNormal);
  }
  // AT+HELP
  else if (cmd == "AT+HELP") {
    Serial.println("AT          - Ping");
    Serial.println("AT+DEVEUI?  - Get DevEUI (read-only)");
    Serial.println("AT+APPKEY?  - Get AppKey");
    Serial.println("AT+APPKEY=  - Set AppKey (32 hex chars)");
    Serial.println("AT+DEVNAME? - Get device name");
    Serial.println("AT+DEVNAME= - Set device name");
    Serial.println("AT+EXIT     - Exit AT mode");
    Serial.println("OK");
  } else {
    Serial.println("ERROR: Unknown command");
  }
}
