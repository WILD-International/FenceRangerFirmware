#ifndef CONFIG_H
#define CONFIG_H

#include <Preferences.h>

#define PREFS_NAMESPACE "fence"

// Max lengths
#define DEVEUI_LEN 8
#define APPKEY_LEN 16
#define DEVNAME_MAX 32

class Config {
 public:
  Config();
  ~Config();
  void load();

  const uint8_t* getDevEUI() const {
    return devEUI;
  }

  const uint64_t getDevEUIAsUint64() const {
    uint64_t id = 0;
    for (int i = 0; i < DEVEUI_LEN; i++) {
      id <<= 8;
      id |= devEUI[i];
    }
    return id;
  }

  const uint8_t* getAppKey() const {
    return appKey;
  }
  bool hasAppKey() const {
    return appKeySet;
  }

  const char* getDevName() const {
    return devName;
  }

  const int32_t getDisplayTimeoutMs() const {
    return display_timeout_ms;
  }

  const float getFenceVoltageMultiplier() const {
    static constexpr float kR1_ohms = 20000000.0f;  // 2x 10MΩ in series
    static constexpr float kR2_ohms = 5100.0f;      // 5.1kΩ
    static constexpr float kFenceVoltageMultiplier = (kR1_ohms + kR2_ohms) / kR2_ohms;

    return kFenceVoltageMultiplier;
  }

  const uint32_t getSleepIntervalS() const {
    return sleepIntervalS;
  }

  const uint32_t getSleepIntervalMs() const {
    return sleepIntervalS * 1000;
  }

  // setters
  void setAppKey(const uint8_t* key);
  void clearAppKey();
  void setDevName(const char* name);
  void setSleepIntervalS(uint32_t s);

 private:
  void loadDevEUI();
  void saveAppKey();
  void saveDevName();
  void saveSleepIntervalS();

  Preferences prefs;

  uint8_t devEUI[DEVEUI_LEN];  // Read-only, from chip
  bool appKeySet = false;
  uint8_t appKey[APPKEY_LEN];
  bool devNameSet = false;
  char devName[DEVNAME_MAX];
  uint32_t sleepIntervalS = 60;
  int32_t display_timeout_ms = 5 * 60 * 1000;
};

#endif  // CONFIG_H
