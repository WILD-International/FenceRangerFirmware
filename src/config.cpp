#include "config.h"

Config::Config() {
  loadDevEUI();
}

Config::~Config() {}

void Config::loadDevEUI() {
  uint32_t uniqueId[2];
  uint64_t id = ESP.getEfuseMac();
  uniqueId[0] = (uint32_t)(id >> 32);
  uniqueId[1] = (uint32_t)id;
  for (int i = 0; i < 8; i++) {
    if (i < 4)
      devEUI[i] = (uniqueId[1] >> (8 * (3 - i))) & 0xFF;
    else
      devEUI[i] = (uniqueId[0] >> (8 * (7 - i))) & 0xFF;
  }
}

void Config::setAppKey(const uint8_t* key) {
  memcpy(appKey, key, APPKEY_LEN);
  saveAppKey();
}

void Config::clearAppKey() {
  prefs.begin(PREFS_NAMESPACE, false);
  if (prefs.isKey("appKey")) {
    prefs.remove("appKey");
  }
  memset(appKey, 0, APPKEY_LEN);
  appKeySet = false;
  prefs.end();
}

void Config::setDevName(const char* name) {
  strlcpy(devName, name, DEVNAME_MAX);
  saveDevName();
}

// --- NVS load/save ---

void Config::load() {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only
  if (prefs.isKey("appKey")) {
    prefs.getBytes("appKey", appKey, APPKEY_LEN);
    appKeySet = true;
  } else {
    memset(appKey, 0, APPKEY_LEN);
    appKeySet = false;
  }
  if (prefs.isKey("devName")) {
    strlcpy(devName, prefs.getString("devName", "no name").c_str(), DEVNAME_MAX);
    devNameSet = true;
  } else {
    memset(devName, 0, DEVNAME_MAX);
    devNameSet = false;
  }
  sleepIntervalS = prefs.getUInt("sleepInterval", 60);
  alertThresholdV = prefs.getUShort("alertV", 3000);
  recoverThresholdV = prefs.getUShort("recoverV", 3500);
  alertBurstSends = prefs.getUChar("burstSends", 3);
  idleHeartbeatCycles = prefs.getUChar("idleHbCycles", 60);
  alertHeartbeatCycles = prefs.getUChar("alertHbCycles", 15);
  recoveryConfirmCycles = prefs.getUChar("recoverCycles", 30);
  prefs.end();
}

void Config::saveAppKey() {
  prefs.begin(PREFS_NAMESPACE, false);
  size_t len = prefs.putBytes("appKey", appKey, APPKEY_LEN);
  if (len == APPKEY_LEN) {
    appKeySet = true;
  }

  prefs.end();
}

void Config::saveDevName() {
  prefs.begin(PREFS_NAMESPACE, false);
  size_t len = prefs.putString("devName", devName);
  if (len == strlen(devName)) {
    devNameSet = true;
  }
  prefs.end();
}

void Config::setSleepIntervalS(uint32_t s) {
  sleepIntervalS = s;
  saveSleepIntervalS();
}

void Config::saveSleepIntervalS() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUInt("sleepInterval", sleepIntervalS);
  prefs.end();
}

void Config::setAlertThresholds(uint16_t alert, uint16_t recover) {
  alertThresholdV = alert;
  recoverThresholdV = recover;
  saveAlertThresholds();
}

void Config::saveAlertThresholds() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUShort("alertV", alertThresholdV);
  prefs.putUShort("recoverV", recoverThresholdV);
  prefs.end();
}

void Config::setAlertCycles(uint8_t burst, uint8_t idleHeartbeat, uint8_t alertHeartbeat,
                            uint8_t recoveryConfirm) {
  alertBurstSends = burst;
  idleHeartbeatCycles = idleHeartbeat;
  alertHeartbeatCycles = alertHeartbeat;
  recoveryConfirmCycles = recoveryConfirm;
  saveAlertCycles();
}

void Config::saveAlertCycles() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUChar("burstSends", alertBurstSends);
  prefs.putUChar("idleHbCycles", idleHeartbeatCycles);
  prefs.putUChar("alertHbCycles", alertHeartbeatCycles);
  prefs.putUChar("recoverCycles", recoveryConfirmCycles);
  prefs.end();
}
