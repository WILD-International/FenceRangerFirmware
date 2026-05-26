#include "at_commands.h"

#include "config.h"
#include "helpers.h"

String serialBuffer;

struct ATCmd {
  const char* name;
  const char* help;
  std::function<String()> get;
  std::function<bool(const String&)> set;  // nullptr = read-only
};

static bool parseIntArg(const String& s, int32_t lo, int32_t hi, int32_t& out) {
  if (s.length() == 0) {
    return false;
  }
  char* end;
  long val = strtol(s.c_str(), &end, 10);
  if (*end != '\0') {
    return false;
  }
  out = (int32_t)val;
  return out >= lo && out <= hi;
}

static void dispatch(const String& raw, Config& config, bool& exitConfigMode) {
  String trimmed = raw;
  trimmed.trim();

  const ATCmd commands[] = {
      {"DEVEUI", "Device EUI (read-only)",
       [&]() -> String {
         char hex[DEVEUI_LEN * 2 + 1] = {};
         bytesToHex(config.getDevEUI(), DEVEUI_LEN, hex);
         return String(hex);
       },
       nullptr},
      {"APPKEY", "AppKey (32 hex chars, empty to clear)",
       [&]() -> String {
         char hex[APPKEY_LEN * 2 + 1] = {};
         bytesToHex(config.getAppKey(), APPKEY_LEN, hex);
         return String(hex);
       },
       [&](const String& v) -> bool {
         if (v.length() == 0) {
           config.clearAppKey();
           return true;
         }
         if (v.length() != APPKEY_LEN * 2) {
           return false;
         }
         uint8_t tmp[APPKEY_LEN];
         if (!hexToBytes(v.c_str(), tmp, APPKEY_LEN)) {
           return false;
         }
         config.setAppKey(tmp);
         return true;
       }},
      {"DEVNAME", "Device name", [&]() -> String { return String(config.getDevName()); },
       [&](const String& v) -> bool {
         if (v.length() == 0 || v.length() >= DEVNAME_MAX) {
           return false;
         }
         config.setDevName(v.c_str());
         return true;
       }},
  };
  const size_t count = sizeof(commands) / sizeof(commands[0]);

  String upper = trimmed;
  upper.toUpperCase();

  if (upper == "AT") {
    Serial.println("OK");
    return;
  }

  if (upper == "AT+EXIT") {
    exitConfigMode = true;
    Serial.println("OK");
    return;
  }

  if (upper == "AT+HELP") {
    static const int kCol = 22;
    char buf[100];
    snprintf(buf, sizeof(buf), "%-*s%s\n", kCol, "AT", "Ping");
    Serial.print(buf);
    snprintf(buf, sizeof(buf), "%-*s%s\n", kCol, "AT+EXIT", "Exit config mode");
    Serial.print(buf);
    snprintf(buf, sizeof(buf), "%-*s%s\n", kCol, "AT+HELP", "Show this help");
    Serial.print(buf);
    for (size_t i = 0; i < count; i++) {
      char cmd[32];
      snprintf(cmd, sizeof(cmd), "AT+%s?", commands[i].name);
      snprintf(buf, sizeof(buf), "%-*sGet %s\n", kCol, cmd, commands[i].help);
      Serial.print(buf);
      if (commands[i].set) {
        snprintf(cmd, sizeof(cmd), "AT+%s=", commands[i].name);
        snprintf(buf, sizeof(buf), "%-*sSet %s\n", kCol, cmd, commands[i].help);
        Serial.print(buf);
      }
    }
    Serial.print("OK\n");
    return;
  }

  if (!upper.startsWith("AT+")) {
    Serial.println("ERROR: Unknown command");
    return;
  }

  // Everything after "AT+": e.g. "DEVNAME?" or "DEVNAME=foo"
  String afterPrefix = upper.substring(3);
  bool isGet = afterPrefix.endsWith("?");
  int eqPos = afterPrefix.indexOf('=');
  bool isSet = eqPos >= 0;

  if (!isGet && !isSet) {
    Serial.println("ERROR: Unknown command");
    return;
  }

  String cmdName;
  String value;

  if (isGet) {
    cmdName = afterPrefix.substring(0, afterPrefix.length() - 1);
  } else {
    cmdName = afterPrefix.substring(0, eqPos);
    value = trimmed.substring(3 + eqPos + 1);  // preserve original case for value
    value.trim();
  }

  if (cmdName.length() == 0) {
    Serial.println("ERROR: Unknown command");
    return;
  }

  for (size_t i = 0; i < count; i++) {
    if (cmdName == commands[i].name) {
      if (isGet) {
        Serial.printf("+%s:%s\r\nOK\r\n", commands[i].name, commands[i].get().c_str());
      } else {
        if (commands[i].set) {
          if (commands[i].set(value)) {
            Serial.println("OK");
          } else {
            Serial.println("ERROR: Invalid value");
          }
        } else {
          Serial.println("ERROR: read-only");
        }
      }
      return;
    }
  }

  Serial.println("ERROR: Unknown command");
}

void handleATSerial(char c, Config& config, bool& exitConfigMode) {
  if (c == '\n' || c == '\r') {
    if (serialBuffer.length() > 0) {
      dispatch(serialBuffer, config, exitConfigMode);
      serialBuffer = "";
    }
  } else {
    serialBuffer += c;
  }

  if (serialBuffer.length() > 128) {
    serialBuffer = "";
  }
}
