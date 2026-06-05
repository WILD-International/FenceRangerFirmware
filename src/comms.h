#ifndef COMMS_H
#define COMMS_H

#include <Preferences.h>
#include <RadioLib.h>

#include <cstdint>

#include "board.h"
#include "config.h"
#include "display.h"
#include "rtc_state.h"
#include "signal_info.h"

// regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;

// Maximum number of consecutive downlinks processed in one wake cycle.
// Additional downlinks queued on the server will be lost.
const int kMaxDownlinkChain = 20;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;

const uint64_t joinEUI = 0x0000000000000000;

enum class DownlinkType : uint8_t {
  None = 0,
  EnableOTA,
  JoinWifiSta,
  SetAlertThresholds,
  SetAlertCycles,
  SetSleepConfig,
  SetSleepAdjRange,
  SetMeasureConfig,
  SetRadioConfig,
  SetDevName,
  QueryAlertThresholds,
  QueryAlertCycles,
  QuerySleepConfig,
  QuerySleepAdjRange,
  QueryMeasureConfig,
  QueryRadioConfig,
  QueryDevName,
  QueryVersion,
};

struct Downlink {
  DownlinkType type = DownlinkType::None;
  union {
    struct {
      uint8_t timeoutMin;               // AP active minutes (0 = default config-mode timeout)
      char password[WIFI_AP_PASS_MAX];  // per-activation AP password ("" = fixed AP_PASSWORD)
    } enableOta;
    struct {
      uint8_t timeoutMin;                // session minutes (0 = default config-mode timeout)
      char ssid[WIFI_SSID_MAX];          // "" = fixed STA_GATEWAY_SSID
      char password[WIFI_STA_PASS_MAX];  // gateway WPA2 password (required, >=8 chars)
    } joinWifiSta;
    struct {
      uint16_t alertV;
      uint16_t recoverV;
    } alertThresholds;
    struct {
      uint8_t burstSends;
      uint8_t idleHbCycles;
      uint8_t alertHbCycles;
      uint8_t recoveryCycles;
    } alertCycles;
    struct {
      uint16_t sleepIntervalS;
      uint8_t sleepMult;
      uint16_t recoveryN;
    } sleepConfig;
    struct {
      uint16_t battFullMv;
      uint16_t battLowMv;
    } sleepAdjRange;
    struct {
      uint16_t measureDurationMs;
      uint16_t fenceCalPercent;
    } measureConfig;
    struct {
      uint8_t rxLnaEnabled;
      int8_t txPower;
      uint8_t dataRate;
      uint8_t adrEnabled;
      uint8_t syncwordPublic;
      uint16_t scanGuardMs;
    } radioConfig;
    struct {
      char name[DEVNAME_MAX];
    } devNameData;
  };
};

class Comms {
 public:
  Comms(Config* config, State* state);

  int16_t begin(Display* display);
  int16_t sendMeasurement(uint16_t battMv, uint16_t fenceV, Display* display, Downlink* downlink,
                          bool confirmed = false, SignalInfo* signalInfo = nullptr);
  int16_t sendConfigReport(uint8_t fport, Display* display, Downlink* downlink);
  Downlink processDownlink(Downlink downlink, Display* display);
  void saveSession();
  bool hasJoined();
  void sleep();
  void resetJoinSchedule();

  // Run one wake of an active radio-config trial: a confirmed-uplink probe at the
  // phase's forced DR, then advance / commit / revert based on whether an ACK arrived.
  void runRadioTrialCycle(uint16_t battMv, uint16_t fenceV, Display* display);

 private:
  bool lwActivate(Display* display);
  int16_t send(uint8_t fport, uint8_t* data, size_t len, Display* display, Downlink* downlink,
               bool confirmed = false, SignalInfo* signalInfo = nullptr);

  // Radio settings actually applied this wake: the NVS config normally, or the
  // staged trial candidate (with ADR forced off / DR forced to testDr) during a trial.
  struct EffectiveRadioConfig {
    bool rxLnaEnabled;
    int8_t txPower;
    bool syncwordPublic;
    uint16_t scanGuardMs;
    bool adrEnabled;
    uint8_t dataRate;
  };
  EffectiveRadioConfig effectiveRadioConfig();

  void stageRadioTrial(const Downlink& downlink);
  void commitRadioTrial();
  void revertRadioTrial();

  bool radioInitialized = false;
  Config* config;
  State* state;
  Preferences store;  // for radiolib persistent data
  Radio radio;
  LoRaWANNode node;
};

#endif  // COMMS_H
