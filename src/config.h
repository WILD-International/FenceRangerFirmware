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

  // setters
  void setAppKey(const uint8_t* key);
  void clearAppKey();
  void setDevName(const char* name);

 private:
  void loadDevEUI();
  void saveAppKey();
  void saveDevName();

  Preferences prefs;

  uint8_t devEUI[DEVEUI_LEN];  // Read-only, from chip
  bool appKeySet = false;
  uint8_t appKey[APPKEY_LEN];
  bool devNameSet = false;
  char devName[DEVNAME_MAX];
};

#endif  // CONFIG_H
