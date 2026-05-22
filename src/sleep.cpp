#include "sleep.h"

#include <driver/rtc_io.h>
#include <esp_private/esp_clk.h>  // for esp_clk_slowclk_cal_get()
#include <soc/rtc.h>

#include "board.h"
#include "logging.h"
#include "esp32-hal-gpio.h"
#include "hal/gpio_types.h"
#include "rtc_state.h"

int64_t rtcTimeNowUs() {
  uint32_t cal = esp_clk_slowclk_cal_get();
  return rtc_time_slowclk_to_us(rtc_time_get(), cal);
}

int64_t getTimeSleptMs() {
  return (rtcTimeNowUs() - rtc.sleepEnterUs) / 1000;
}

// On wake: release the GPIO holds that kept pin states stable during deep sleep,
// then set each pin to its initial state for this cycle.
void gpioWakeInit() {
  gpio_deep_sleep_hold_dis();

  rtc_gpio_hold_dis(gpio_num_t(RADIO_NSS));

  gpio_hold_dis(gpio_num_t(LED));
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);

#if defined(USE_GC1109_PA)
  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_EN);
  pinMode(LORA_PA_EN, OUTPUT);
  digitalWrite(LORA_PA_EN, LOW);
#endif
#if defined(USE_KCT8103L_PA)
  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_CSD);
  pinMode(LORA_PA_CSD, OUTPUT);
  digitalWrite(LORA_PA_CSD, LOW);
#endif
}

// Before sleep: drive each pin to its safe sleep state, then latch the holds
// so those states are preserved while the CPU is powered down.
void gpioSleepLatch() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  gpio_hold_en(gpio_num_t(LED));

  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
  rtc_gpio_hold_en((gpio_num_t)LORA_PA_POWER);

  pinMode(RADIO_NSS, OUTPUT);
  digitalWrite(RADIO_NSS, HIGH);
  rtc_gpio_hold_en(gpio_num_t(RADIO_NSS));

#if defined(USE_GC1109_PA)
  pinMode(LORA_PA_EN, OUTPUT);
  digitalWrite(LORA_PA_EN, LOW);
  rtc_gpio_hold_en((gpio_num_t)LORA_PA_EN);
#endif
#if defined(USE_KCT8103L_PA)
  pinMode(LORA_PA_CSD, OUTPUT);
  digitalWrite(LORA_PA_CSD, LOW);
  rtc_gpio_hold_en((gpio_num_t)LORA_PA_CSD);
  delay(1);
#endif

  gpio_deep_sleep_hold_en();
}

void gotoSleep(uint32_t ms, bool setSleepTime) {
  if (setSleepTime) {
    rtc.sleepEnterUs = rtcTimeNowUs();
    rtc.sleepTimeMs = ms;
  }
  esp_sleep_enable_timer_wakeup(ms * 1000UL);  // function uses uS
  LOGLN(F("Sleeping\n"));
  Serial.flush();

  gpioSleepLatch();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
  esp_deep_sleep_start();

  // if this appears in the serial debug, we didn't go to sleep!
  // so take defensive action so we don't continually uplink
  Serial.println(F("\n\n### Sleep failed, delay of 5 minutes & then restart ###\n"));
  delay(5UL * 60UL * 1000UL);
  ESP.restart();
}
