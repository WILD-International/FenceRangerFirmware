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

void Config::setDevName(const char* name) {
  strlcpy(devName, name, DEVNAME_MAX);
  saveDevName();
}

// --- NVS load/save ---

void Config::load() {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only
  prefs.getBytes("appKey", appKey, APPKEY_LEN);
  strlcpy(devName, prefs.getString("devName", "???").c_str(), DEVNAME_MAX);
  prefs.end();
}

void Config::saveAppKey() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBytes("appKey", appKey, APPKEY_LEN);
  prefs.end();
}

void Config::saveDevName() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("devName", devName);
  prefs.end();
}
