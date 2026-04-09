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
  const uint8_t* getAppKey() const {
    return appKey;
  }
  const char* getDevName() const {
    return devName;
  }

  // setters
  void setAppKey(const uint8_t* key);
  void setDevName(const char* name);

 private:
  void loadDevEUI();
  void saveAppKey();
  void saveDevName();

  Preferences prefs;

  uint8_t devEUI[DEVEUI_LEN];  // Read-only, from chip
  uint8_t appKey[APPKEY_LEN];
  char devName[DEVNAME_MAX];
};

#endif  // CONFIG_H
