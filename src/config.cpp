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
    if (i < 4) {
      devEUI[i] = (uniqueId[1] >> (8 * (3 - i))) & 0xFF;
    } else {
      devEUI[i] = (uniqueId[0] >> (8 * (7 - i))) & 0xFF;
    }
  }
}

// --- NVS load/save ---

void Config::load() {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only
  if (prefs.isKey("devEUI")) {
    prefs.getBytes("devEUI", devEUI, DEVEUI_LEN);
    devEUIOverride = true;
  }
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
  alertThresholdV = prefs.getUShort("alertV", 2000);
  recoverThresholdV = prefs.getUShort("recoverV", 2500);
  alertBurstSends = prefs.getUChar("burstSends", 3);
  idleHeartbeatCycles = prefs.getUChar("idleHbCycles", 60);
  alertHeartbeatCycles = prefs.getUChar("alertHbCycles", 15);
  recoveryConfirmCycles = prefs.getUChar("recoverCycles", 30);
  rapidDropPercent = prefs.getUChar("dropPct", 10);
  battFullMv = prefs.getUShort("battFullMv", 3800);
  battLowMv = prefs.getUShort("battLowMv", 3300);
  battSleepMult = prefs.getUChar("battSleepMult", 30);
  battRecoveryN = prefs.getUShort("battRecoveryN", 50);
  rxLnaEnabled = prefs.getBool("rxLnaEnabled", true);
  measureDurationMs = prefs.getUShort("measureMs", 3000);
  fenceCalPercent = prefs.getUShort("fenceCalPct", 100);
  txPower = (int8_t)prefs.getChar("txPower", 10);
  dataRate = prefs.getUChar("dataRate", 3);
  carrierRev = prefs.getUChar("carrierRev", 1);
  adrEnabled = prefs.getBool("adrEnabled", true);
  syncwordPublic = prefs.getBool("syncwordPublic", true);
  scanGuardMs = prefs.getUShort("scanGuard", 100);
  prefs.end();
}

void Config::setFenceCalPercent(uint16_t percent) {
  fenceCalPercent = percent;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUShort("fenceCalPct", fenceCalPercent);
  prefs.end();
}

void Config::setMeasureDurationMs(uint16_t ms) {
  measureDurationMs = ms;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUShort("measureMs", measureDurationMs);
  prefs.end();
}

void Config::setRxLnaEnabled(bool enabled) {
  rxLnaEnabled = enabled;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBool("rxLnaEnabled", rxLnaEnabled);
  prefs.end();
}

void Config::setAppKey(const uint8_t* key) {
  memcpy(appKey, key, APPKEY_LEN);
  prefs.begin(PREFS_NAMESPACE, false);
  size_t len = prefs.putBytes("appKey", appKey, APPKEY_LEN);
  if (len == APPKEY_LEN) {
    appKeySet = true;
  }
  prefs.end();
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

void Config::setDevEUI(const uint8_t* eui) {
  memcpy(devEUI, eui, DEVEUI_LEN);
  devEUIOverride = true;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBytes("devEUI", devEUI, DEVEUI_LEN);
  prefs.end();
}

void Config::clearDevEUI() {
  devEUIOverride = false;
  prefs.begin(PREFS_NAMESPACE, false);
  if (prefs.isKey("devEUI")) {
    prefs.remove("devEUI");
  }
  prefs.end();
  loadDevEUI();
}

void Config::setDevName(const char* name) {
  strlcpy(devName, name, DEVNAME_MAX);
  prefs.begin(PREFS_NAMESPACE, false);
  size_t len = prefs.putString("devName", devName);
  if (len == strlen(devName)) {
    devNameSet = true;
  }
  prefs.end();
}

void Config::setSleepIntervalS(uint16_t s) {
  sleepIntervalS = s;
  prefs.begin(PREFS_NAMESPACE, false);
  // Keep as UInt (not UShort) to not break existing configs
  prefs.putUInt("sleepInterval", sleepIntervalS);
  prefs.end();
}

void Config::setAlertThresholds(uint16_t alert, uint16_t recover) {
  alertThresholdV = alert;
  recoverThresholdV = recover;
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
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUChar("burstSends", alertBurstSends);
  prefs.putUChar("idleHbCycles", idleHeartbeatCycles);
  prefs.putUChar("alertHbCycles", alertHeartbeatCycles);
  prefs.putUChar("recoverCycles", recoveryConfirmCycles);
  prefs.end();
}

void Config::setRapidDropPercent(uint8_t percent) {
  rapidDropPercent = percent;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUChar("dropPct", rapidDropPercent);
  prefs.end();
}

void Config::setTxPower(int8_t power) {
  txPower = power;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putChar("txPower", txPower);
  prefs.end();
}

void Config::setDataRate(uint8_t dr) {
  dataRate = dr;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUChar("dataRate", dataRate);
  prefs.end();
}

void Config::setCarrierRev(uint8_t rev) {
  carrierRev = rev;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUChar("carrierRev", carrierRev);
  prefs.end();
}

void Config::setSyncwordPublic(bool isPublic) {
  syncwordPublic = isPublic;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBool("syncwordPublic", syncwordPublic);
  prefs.end();
}

void Config::setScanGuardMs(uint16_t ms) {
  scanGuardMs = ms;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUShort("scanGuard", scanGuardMs);
  prefs.end();
}

void Config::setAdrEnabled(bool enabled) {
  adrEnabled = enabled;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBool("adrEnabled", adrEnabled);
  prefs.end();
}

void Config::setBattAdaptiveSleep(uint16_t fullMv, uint16_t lowMv, uint8_t sleepMult,
                                  uint16_t recoveryN) {
  battFullMv = fullMv;
  battLowMv = lowMv;
  battSleepMult = sleepMult;
  battRecoveryN = recoveryN;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUShort("battFullMv", battFullMv);
  prefs.putUShort("battLowMv", battLowMv);
  prefs.putUChar("battSleepMult", battSleepMult);
  prefs.putUShort("battRecoveryN", battRecoveryN);
  prefs.end();
}
