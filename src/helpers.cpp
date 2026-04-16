#include "helpers.h"

#include <driver/rtc_io.h>
#include <esp_private/esp_clk.h>  // for esp_clk_slowclk_cal_get()
#include <soc/rtc.h>

#include "board.h"
#include "debug.h"
#include "esp32-hal-gpio.h"
#include "hal/gpio_types.h"

RTC_DATA_ATTR int64_t sleep_enter_us = 0;
RTC_DATA_ATTR int64_t sleep_time_ms = 0;

int64_t rtcTimeNowUs() {
  uint32_t cal = esp_clk_slowclk_cal_get();
  return rtc_time_slowclk_to_us(rtc_time_get(), cal);
}

// put device in to lowest power deep-sleep mode
void gotoSleep(uint32_t ms, bool setSleepTime) {
  if (setSleepTime) {
    sleep_enter_us = rtcTimeNowUs();
    sleep_time_ms = ms;
  }
  esp_sleep_enable_timer_wakeup(ms * 1000UL);  // function uses uS
  LOGLN(F("Sleeping\n"));
  Serial.flush();

  pinMode(RADIO_NSS, OUTPUT);
  digitalWrite(RADIO_NSS, HIGH);
  rtc_gpio_hold_en(gpio_num_t(RADIO_NSS));

  // Keep the LED off in deep sleep
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  gpio_hold_en(gpio_num_t(LED));  // hold state through deep sleep

  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
  esp_deep_sleep_start();

  // if this appears in the serial debug, we didn't go to sleep!
  // so take defensive action so we don't continually uplink
  Serial.println(F("\n\n### Sleep failed, delay of 5 minutes & then restart ###\n"));
  delay(5UL * 60UL * 1000UL);
  ESP.restart();
}

int64_t getTimeSleptMs() {
  return (rtcTimeNowUs() - sleep_enter_us) / 1000;
}

uint32_t getRemainingSleepTimeMs() {
  int64_t slept_ms = getTimeSleptMs();
  int64_t remaining_ms = sleep_time_ms - slept_ms;
  return remaining_ms > 0 ? remaining_ms : 0;
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
