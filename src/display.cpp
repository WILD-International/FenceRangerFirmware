#include "display.h"

#include "board.h"
#include "esp32-hal-gpio.h"
#include "helpers.h"

const char* title = "WILD FenceRanger";

Display::Display(Config* config)
    : enabled(false), config(config), display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST) {}

Display::~Display() {}

void Display::message(const char* msg, uint32_t delayMs) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayTitle();
  display.setTextSize(2);
  display.setCursor((SCREEN_WIDTH - strlen(msg) * 12) / 2, 16);
  display.println(msg);
  display.display();
  if (delayMs > 0) {
    delay(delayMs);
  }
}

void Display::debugModeProgressBar(uint32_t progress, bool enable) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayTitle();
  if (progress > 100) {
    progress = 100;
  }
  const char* enable_msg = "Enable debug mode";
  const char* disable_msg = "Disable debug mode";
  const char* msg = enable ? enable_msg : disable_msg;
  display.setCursor((SCREEN_WIDTH - strlen(msg) * 6) / 2, (SCREEN_HEIGHT - 8) / 2);
  display.println(msg);
  display.fillRect(0, 56, SCREEN_WIDTH * progress / 100, 8, SSD1306_WHITE);
  display.display();
}

void Display::showMeasurement(State& state, uint32_t delayMs) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayTitle();
  display.setCursor(0, 16);
  display.printf("batt: %d %d", state.batt_min_mv, state.batt_max_mv);
  display.setCursor(0, 28);
  display.printf(" neg: %d %d", state.fence_neg_min_v, state.fence_neg_max_v);
  display.setCursor(0, 40);
  display.printf(" pos: %d %d", state.fence_pos_min_v, state.fence_pos_max_v);
  display.setCursor(0, 52);
  display.printf(" err: %d", state.adc_error_count);
  //
  display.display();
  if (delayMs > 0) {
    delay(delayMs);
  }
}

void Display::showConfig(State& state) {
  if (!enabled) {
    return;
  }
  char hex[DEVEUI_LEN * 2 + 1] = {};

  display.clearDisplay();
  displayTitle();
  display.setCursor(2, display.getCursorY());
  display.println("Name:");
  display.setCursor(2, display.getCursorY());
  display.println(config->getDevName());
  display.setCursor(2, 34);
  display.println("Device EUI:");
  display.setCursor(2, display.getCursorY());
  bytesToHex(config->getDevEUI(), DEVEUI_LEN, hex);
  display.println(hex);
  display.setCursor(2, 54);
  display.print("ver: ");
  display.print(GIT_TAG);
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
  displayTitle();
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

void Display::displayTitle() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor((SCREEN_WIDTH - strlen(title) * 6) / 2, 2);
  display.println(title);
}
