#include "display.h"

#include "board.h"
#include "esp32-hal-gpio.h"
#include "helpers.h"

RTC_DATA_ATTR int64_t display_on_us = 0;

Display::Display(Config* config)
    : enabled(false), config(config), display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST) {}

Display::~Display() {}

void Display::begin() {
  int64_t now_us = rtcTimeNowUs();
  if (now_us - display_on_us < config->getDisplayTimeoutMs() * 1000) {
    turnOn();
  }
}

void Display::trigger() {
  display_on_us = rtcTimeNowUs();
  turnOn();
}

void Display::message(const char* msg, uint32_t delayMs) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 2);
  display.println("WILD FenceRanger");
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println(msg);
  display.display();
  if (delayMs > 0) {
    delay(delayMs);
  }
}

void Display::showMeasurement(State& state) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 2);
  display.println("WILD FenceRanger");
  display.setCursor(0, 16);
  display.printf("batt: %d %d", state.batt_min_mv, state.batt_max_mv);
  display.setCursor(0, 28);
  display.printf(" neg: %d %d", state.fence_neg_min_mv, state.fence_neg_max_mv);
  display.setCursor(0, 40);
  display.printf(" pos: %d %d", state.fence_pos_min_mv, state.fence_pos_max_mv);
  display.setCursor(0, 52);
  display.printf(" err: %d", state.adc_error_count);
  //
  display.display();
}

void Display::turnOn() {
  if (enabled) {
    return;
  }
  // Turn on the display and initialize
  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, LOW);  // power on the display
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Wire.end();
    digitalWrite(DISPLAY_POWER, HIGH);
    return;  // don't change state if init failed
  }
  enabled = true;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 2);
  display.println("WILD FenceRanger");
  display.display();
}

void Display::turnOff() {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  display.display();
  Wire.end();
  digitalWrite(DISPLAY_POWER, HIGH);
  enabled = false;
}
