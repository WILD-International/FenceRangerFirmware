#include "at_commands.h"

#include "config.h"
#include "helpers.h"
#include "version.h"

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
      {"VERSION", "Firmware version", [&]() -> String { return String(GIT_TAG); }, nullptr},
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

      // Alert config
      {"ALERTV", "Alert threshold (V)",
       [&]() -> String { return String(config.getAlertThresholdV()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 65535, n))
           return false;
         config.setAlertThresholdV(n);
         return true;
       }},
      {"RECOVERV", "Recovery threshold (V)",
       [&]() -> String { return String(config.getRecoverThresholdV()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 65535, n))
           return false;
         config.setRecoverThresholdV(n);
         return true;
       }},
      {"BURSTSENDS", "Alert burst sends",
       [&]() -> String { return String(config.getAlertBurstSends()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 255, n))
           return false;
         config.setAlertBurstSends(n);
         return true;
       }},
      {"IDLEHBCYCLES", "Idle heartbeat (cycles)",
       [&]() -> String { return String(config.getIdleHeartbeatCycles()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 255, n))
           return false;
         config.setIdleHeartbeatCycles(n);
         return true;
       }},
      {"ALERTHBCYCLES", "Alert heartbeat (cycles)",
       [&]() -> String { return String(config.getAlertHeartbeatCycles()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 255, n))
           return false;
         config.setAlertHeartbeatCycles(n);
         return true;
       }},
      {"RECOVERCYCLES", "Recovery confirm (cycles)",
       [&]() -> String { return String(config.getRecoveryConfirmCycles()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 255, n))
           return false;
         config.setRecoveryConfirmCycles(n);
         return true;
       }},

      // Sleep config
      {"SLEEPINTERVAL", "Cycle sleep (s)",
       [&]() -> String { return String(config.getSleepIntervalS()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 65535, n))
           return false;
         config.setSleepIntervalS(n);
         return true;
       }},
      {"BATTFULLMV", "Battery full (mV)",
       [&]() -> String { return String(config.getBattFullMv()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 65535, n))
           return false;
         config.setBattFullMv(n);
         return true;
       }},
      {"BATTLOWMV", "Battery low (mV)", [&]() -> String { return String(config.getBattLowMv()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 65535, n))
           return false;
         config.setBattLowMv(n);
         return true;
       }},
      {"BATTSLEEPMULT", "Sleep multiplier",
       [&]() -> String { return String(config.getBattSleepMult()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 255, n))
           return false;
         config.setBattSleepMult(n);
         return true;
       }},
      {"BATTRECOVERYN", "Slow filter N (batt recovery)",
       [&]() -> String { return String(config.getBattRecoveryN()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 65535, n))
           return false;
         config.setBattRecoveryN(n);
         return true;
       }},

      // Measure config
      {"MEASUREDURATION", "Measure duration (ms)",
       [&]() -> String { return String(config.getMeasureDurationMs()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 100, 60000, n))
           return false;
         config.setMeasureDurationMs(n);
         return true;
       }},
      {"FENCECAL", "Fence calibration (%)",
       [&]() -> String { return String(config.getFenceCalPercent()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 1, 1000, n))
           return false;
         config.setFenceCalPercent(n);
         return true;
       }},

      // Radio config
      {"RXLNA", "RX LNA enabled (0/1)",
       [&]() -> String { return String(config.getRxLnaEnabled() ? 1 : 0); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 1, n))
           return false;
         config.setRxLnaEnabled(n != 0);
         return true;
       }},
      {"TXPOWER", "TX power (dBm)", [&]() -> String { return String(config.getTxPower()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 22, n))
           return false;
         config.setTxPower(n);
         return true;
       }},
      {"SF", "Spreading factor (7-12)",
       [&]() -> String { return String(config.getSpreadingFactor()); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 7, 12, n))
           return false;
         config.setSpreadingFactor(n);
         return true;
       }},
      {"ADR", "Adaptive data rate (0/1)",
       [&]() -> String { return String(config.getAdrEnabled() ? 1 : 0); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 1, n))
           return false;
         config.setAdrEnabled(n != 0);
         return true;
       }},
      {"SYNCWORD", "Sync word public (0/1)",
       [&]() -> String { return String(config.getSyncwordPublic() ? 1 : 0); },
       [&](const String& v) -> bool {
         int32_t n;
         if (!parseIntArg(v, 0, 1, n))
           return false;
         config.setSyncwordPublic(n != 0);
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
        Serial.printf("+%s:%s\nOK\n", commands[i].name, commands[i].get().c_str());
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
