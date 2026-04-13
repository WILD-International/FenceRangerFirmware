#include "helpers.h"

#include "debug.h"

// put device in to lowest power deep-sleep mode
void gotoSleep(uint32_t seconds) {
  esp_sleep_enable_timer_wakeup(seconds * 1000UL * 1000UL);  // function uses uS
  LOGLN(F("Sleeping\n"));
  Serial.flush();

  esp_deep_sleep_start();

  // if this appears in the serial debug, we didn't go to sleep!
  // so take defensive action so we don't continually uplink
  Serial.println(F("\n\n### Sleep failed, delay of 5 minutes & then restart ###\n"));
  delay(5UL * 60UL * 1000UL);
  ESP.restart();
}

void bytesToHex(const uint8_t* data, size_t len, char* out) {
  for (size_t i = 0; i < len; i++) {
    sprintf(out + i * 2, "%02X", data[i]);
  }
}

bool hexToBytes(const char* hex, uint8_t* out, size_t expectedLen) {
  size_t hexLen = strlen(hex);
  if (hexLen != expectedLen * 2)
    return false;
  for (size_t i = 0; i < expectedLen; i++) {
    char byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char* end;
    unsigned long val = strtoul(byte, &end, 16);
    if (*end != '\0')
      return false;
    out[i] = (uint8_t)val;
  }
  return true;
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      LOGLN("Wakeup caused by external signal using RTC_IO");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      LOGLN("Wakeup caused by external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      LOGLN("Wakeup caused by timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      LOGLN("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      LOGLN("Wakeup caused by ULP program");
      break;
    default:
      LOG("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
      break;
  }
}
