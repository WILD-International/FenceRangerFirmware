#include "sleep.h"

#include <SPI.h>
#include <driver/rtc_io.h>
#include <esp_private/esp_clk.h>  // for esp_clk_slowclk_cal_get()
#include <soc/rtc.h>

#include "board.h"
#include "carrier_board.h"
#include "config.h"
#include "esp32-hal-gpio.h"
#include "hal/gpio_types.h"
#include "logging.h"
#include "rtc_state.h"

// The pin that controls the PA: an enable pin on the GC1109, a CSD (chip-shutdown) pin on the
// KCT8103L. They behave differently but both land on GPIO 2; this only resolves which GPIO the
// PA occupies on this build, so the carrier-board logic can tell whether the opamp shares it.
static gpio_num_t paControlPin() {
#if defined(USE_GC1109_PA)
  return (gpio_num_t)LORA_PA_EN;
#else
  return (gpio_num_t)LORA_PA_CSD;
#endif
}

int64_t rtcTimeNowUs() {
  uint32_t cal = esp_clk_slowclk_cal_get();
  return rtc_time_slowclk_to_us(rtc_time_get(), cal);
}

int64_t getTimeSleptMs() {
  return (rtcTimeNowUs() - rtc.sleepEnterUs) / 1000;
}

// Release any hold/isolation and return the pad to a defined output-low state on wake.
static void wakePaPin(gpio_num_t pin) {
  rtc_gpio_hold_dis(pin);
  rtc_gpio_deinit(pin);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// On wake: release the GPIO holds that kept pin states stable during deep sleep,
// then set each pin to its initial state for this cycle.
void gpioWakeInit(Config& config) {
  gpio_deep_sleep_hold_dis();

  gpio_hold_dis(gpio_num_t(LED));
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // Release the OLED/Vext rail and default it off; Display::turnOn() drives it on when needed.
  gpio_hold_dis(gpio_num_t(DISPLAY_POWER));
  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, HIGH);  // active-low rail: HIGH = off

  // Release the battery-sense divider enable so MeasureProgram can drive it again.
  gpio_hold_dis(gpio_num_t(ADC_CTRL_VBAT));

  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
  rtc_gpio_deinit((gpio_num_t)LORA_PA_POWER);
  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);

  gpio_num_t paPin = paControlPin();
  wakePaPin(paPin);

  // Release the carrier-board opamp pin (floated during sleep) when it's a distinct GPIO (v3.1),
  // so MeasureProgram can drive it. On v3.0 it's GPIO 2 and wakePaPin above already covers it.
  gpio_num_t opampPin = carrierBoardConfig(config.getCarrierRev()).opampEnablePin;
  if (opampPin != paPin) {
    rtc_gpio_hold_dis(opampPin);
    rtc_gpio_deinit(opampPin);
  }
}

// Before sleep: drive each pin to its safe sleep state, then latch the holds
// so those states are preserved while the CPU is powered down.
void gpioSleepLatch(Config& config) {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  gpio_hold_en(gpio_num_t(LED));

  // Power down the OLED/Vext rail and hold it off through deep sleep. GPIO 36 isn't an RTC pad,
  // so without an explicit hold it floats while asleep and the display stays on, drawing ~2.4 mA.
  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, HIGH);  // active-low rail: HIGH = off
  gpio_hold_en(gpio_num_t(DISPLAY_POWER));

  // Disable the battery-sense divider (its MOSFET draws ~9 µA when on) and hold it off.
  pinMode(ADC_CTRL_VBAT, OUTPUT);
  digitalWrite(ADC_CTRL_VBAT, LOW);
  gpio_hold_en(gpio_num_t(ADC_CTRL_VBAT));

  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
  rtc_gpio_hold_en((gpio_num_t)LORA_PA_POWER);
  rtc_gpio_isolate((gpio_num_t)LORA_PA_POWER);

  // Quiesce the radio SPI bus as the Heltec factory example does: end the SPI peripheral and
  // float every radio-side pin. gpio_deep_sleep_hold_en() below holds *all* digital pads, so any
  // pin left driven would keep sourcing current into the sleeping SX1262 / unpowered PA.
  SPI.end();
  pinMode(HELTEC_DIO1, ANALOG);
  pinMode(HELTEC_LORA_NSS, ANALOG);
  pinMode(HELTEC_LORA_RST, ANALOG);
  pinMode(HELTEC_LORA_BUSY, ANALOG);
  pinMode(HELTEC_LORA_SCK, ANALOG);
  pinMode(HELTEC_LORA_MISO, ANALOG);
  pinMode(HELTEC_LORA_MOSI, ANALOG);

  // Float the OLED pins. After a config session Wire.end() leaves the internal I2C pull-ups
  // enabled on SDA/SCL and the driver leaves RST driven high; with the Vext rail off, all three
  // leak into the unpowered display (~50-70 uA per pulled-up pin through the ~45 kOhm pulls).
  pinMode(OLED_SDA, ANALOG);
  pinMode(OLED_SCL, ANALOG);
  pinMode(OLED_RST, ANALOG);

  // The carrier-board opamp-enable pin floats during deep sleep on both revs. On v3.1 the opamp
  // moved off GPIO 2, so the PA pin is distinct and returns to its low+hold sleep state; on v3.0
  // the opamp pin *is* GPIO 2, so the float governs it and the PA pin needs no separate latch.
  gpio_num_t paPin = paControlPin();
  gpio_num_t opampPin = carrierBoardConfig(config.getCarrierRev()).opampEnablePin;
  rtc_gpio_hold_dis(opampPin);
  rtc_gpio_isolate(opampPin);
  if (opampPin != paPin) {
    pinMode(paPin, OUTPUT);
    digitalWrite(paPin, LOW);
    rtc_gpio_hold_en(paPin);
  }
#if defined(USE_KCT8103L_PA)
  delay(1);
#endif

  gpio_deep_sleep_hold_en();
}

void gotoSleep(Config& config, uint32_t ms, bool setSleepTime) {
  if (setSleepTime) {
    rtc.sleepEnterUs = rtcTimeNowUs();
    rtc.sleepTimeMs = ms;
  }
  esp_sleep_enable_timer_wakeup(ms * 1000UL);  // function uses uS
  LOGLN(F("Sleeping\n"));
  Serial.flush();

  gpioSleepLatch(config);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
  esp_deep_sleep_start();

  // if this appears in the serial debug, we didn't go to sleep!
  // so take defensive action so we don't continually uplink
  Serial.println(F("\n\n### Sleep failed, delay of 5 minutes & then restart ###\n"));
  delay(5UL * 60UL * 1000UL);
  ESP.restart();
}
