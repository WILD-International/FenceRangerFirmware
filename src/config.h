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

  uint16_t getAlertThresholdV() const {
    return alertThresholdV;
  }
  uint16_t getRecoverThresholdV() const {
    return recoverThresholdV;
  }
  uint8_t getAlertBurstSends() const {
    return alertBurstSends;
  }
  uint8_t getIdleHeartbeatCycles() const {
    return idleHeartbeatCycles;
  }
  uint8_t getAlertHeartbeatCycles() const {
    return alertHeartbeatCycles;
  }
  uint8_t getRecoveryConfirmCycles() const {
    return recoveryConfirmCycles;
  }

  uint16_t getBattFullMv() const {
    return battFullMv;
  }
  uint16_t getBattLowMv() const {
    return battLowMv;
  }
  uint8_t getBattSleepMult() const {
    return battSleepMult;
  }
  uint16_t getBattRecoveryN() const {
    return battRecoveryN;
  }

  // setters
  void setAppKey(const uint8_t* key);
  void clearAppKey();
  void setDevName(const char* name);
  void setSleepIntervalS(uint32_t s);
  void setAlertThresholds(uint16_t alert, uint16_t recover);
  void setAlertCycles(uint8_t burst, uint8_t idleHeartbeat, uint8_t alertHeartbeat,
                      uint8_t recoveryConfirm);
  void setBattAdaptiveSleep(uint16_t fullMv, uint16_t lowMv, uint8_t sleepMult, uint16_t recoveryN);

 private:
  void loadDevEUI();

  Preferences prefs;

  uint8_t devEUI[DEVEUI_LEN];  // Read-only, from chip
  bool appKeySet = false;
  uint8_t appKey[APPKEY_LEN];
  bool devNameSet = false;
  char devName[DEVNAME_MAX];
  uint32_t sleepIntervalS = 60;
  uint16_t alertThresholdV = 3000;
  uint16_t recoverThresholdV = 3500;
  uint8_t alertBurstSends = 3;
  uint8_t idleHeartbeatCycles = 60;
  uint8_t alertHeartbeatCycles = 15;
  uint8_t recoveryConfirmCycles = 30;
  uint16_t battFullMv = 4000;
  uint16_t battLowMv = 3300;
  uint8_t battSleepMult = 30;
  uint16_t battRecoveryN = 50;
  int32_t display_timeout_ms = 5 * 60 * 1000;
};

#endif  // CONFIG_H
