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

  bool hasCustomDevEUI() const {
    return devEUIOverride;
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
    static constexpr float kBaseMultiplier = (kR1_ohms + kR2_ohms) / kR2_ohms;
    return kBaseMultiplier * fenceCalPercent / 100.0f;
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

  bool getRxLnaEnabled() const {
    return rxLnaEnabled;
  }
  void setRxLnaEnabled(bool enabled);

  uint16_t getMeasureDurationMs() const {
    return measureDurationMs;
  }
  void setMeasureDurationMs(uint16_t ms);

  uint16_t getFenceCalPercent() const {
    return fenceCalPercent;
  }
  void setFenceCalPercent(uint16_t percent);

  int8_t getTxPower() const {
    return txPower;
  }
  void setTxPower(int8_t power);

  uint8_t getDataRate() const {
    return dataRate;
  }
  void setDataRate(uint8_t dr);

  // Carrier-board revision: 0=v3.0 (opamp GPIO 2), 1=v3.1 (opamp GPIO 6).
  // Picks the runtime opamp pin mapping (see carrier_board.h). Never settable over LoRa.
  uint8_t getCarrierRev() const {
    return carrierRev;
  }
  void setCarrierRev(uint8_t rev);

  bool getAdrEnabled() const {
    return adrEnabled;
  }
  void setAdrEnabled(bool enabled);

  bool getSyncwordPublic() const {
    return syncwordPublic;
  }
  void setSyncwordPublic(bool isPublic);

  // RX-window guard padding in ms (RadioLib LoRaWANNode::scanGuard, default 10)
  uint16_t getScanGuardMs() const {
    return scanGuardMs;
  }
  void setScanGuardMs(uint16_t ms);

  // Grouped setters (used by LoRaWAN downlinks and web UI)
  void setAppKey(const uint8_t* key);
  void clearAppKey();
  void setDevEUI(const uint8_t* eui);
  void clearDevEUI();
  void setDevName(const char* name);
  void setSleepIntervalS(uint16_t s);
  void setAlertThresholds(uint16_t alert, uint16_t recover);
  void setAlertCycles(uint8_t burst, uint8_t idleHeartbeat, uint8_t alertHeartbeat,
                      uint8_t recoveryConfirm);
  void setBattAdaptiveSleep(uint16_t fullMv, uint16_t lowMv, uint8_t sleepMult, uint16_t recoveryN);

  // Individual setters (delegate to grouped setters above)
  void setAlertThresholdV(uint16_t value) {
    setAlertThresholds(value, recoverThresholdV);
  }
  void setRecoverThresholdV(uint16_t value) {
    setAlertThresholds(alertThresholdV, value);
  }
  void setAlertBurstSends(uint16_t value) {
    setAlertCycles(value, idleHeartbeatCycles, alertHeartbeatCycles, recoveryConfirmCycles);
  }
  void setIdleHeartbeatCycles(uint16_t value) {
    setAlertCycles(alertBurstSends, value, alertHeartbeatCycles, recoveryConfirmCycles);
  }
  void setAlertHeartbeatCycles(uint16_t value) {
    setAlertCycles(alertBurstSends, idleHeartbeatCycles, value, recoveryConfirmCycles);
  }
  void setRecoveryConfirmCycles(uint16_t value) {
    setAlertCycles(alertBurstSends, idleHeartbeatCycles, alertHeartbeatCycles, value);
  }
  void setBattFullMv(uint16_t value) {
    setBattAdaptiveSleep(value, battLowMv, battSleepMult, battRecoveryN);
  }
  void setBattLowMv(uint16_t value) {
    setBattAdaptiveSleep(battFullMv, value, battSleepMult, battRecoveryN);
  }
  void setBattSleepMult(uint16_t value) {
    setBattAdaptiveSleep(battFullMv, battLowMv, (uint8_t)value, battRecoveryN);
  }
  void setBattRecoveryN(uint16_t value) {
    setBattAdaptiveSleep(battFullMv, battLowMv, battSleepMult, value);
  }

 private:
  void loadDevEUI();

  Preferences prefs;

  uint8_t devEUI[DEVEUI_LEN];
  bool devEUIOverride = false;
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
  bool rxLnaEnabled = true;
  uint16_t measureDurationMs = 3000;
  uint16_t fenceCalPercent = 100;
  int8_t txPower = 10;
  uint8_t dataRate = 3;
  uint8_t carrierRev = 0;  // 0=v3.0 (opamp GPIO2) 1=v3.1 (opamp GPIO6)
  bool adrEnabled = true;
  bool syncwordPublic = true;
  uint16_t scanGuardMs = 10;
  int32_t display_timeout_ms = 5 * 60 * 1000;
};

#endif  // CONFIG_H
