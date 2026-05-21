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

void Display::showMenu(int selectedIndex, int scrollOffset, bool debugEnabled) {
  if (!enabled) {
    return;
  }

  const int TOTAL_ITEMS = 13;
  const int VISIBLE_ITEMS = 6;
  const int SCROLLBAR_X = 124;
  const int SCROLLBAR_W = 4;
  const int TRACK_Y = 11;
  const int TRACK_H = 53;

  char labels[13][22];
  snprintf(labels[0], 22, "%-14s%5d", "Cycle Sleep", config->getSleepIntervalS());
  snprintf(labels[1], 22, "%-14s%5d", "Burst Sends", config->getAlertBurstSends());
  snprintf(labels[2], 22, "%-14s%5d", "Idle Hb", config->getIdleHeartbeatCycles());
  snprintf(labels[3], 22, "%-14s%5d", "Alert Hb", config->getAlertHeartbeatCycles());
  snprintf(labels[4], 22, "%-14s%5d", "Recovery", config->getRecoveryConfirmCycles());
  snprintf(labels[5], 22, "%-14s%5d", "Alert V", config->getAlertThresholdV());
  snprintf(labels[6], 22, "%-14s%5d", "Recover V", config->getRecoverThresholdV());
  snprintf(labels[7], 22, "%-14s%5d", "Batt Full mV", config->getBattFullMv());
  snprintf(labels[8], 22, "%-14s%5d", "Batt Low mV", config->getBattLowMv());
  snprintf(labels[9], 22, "%-14s%5d", "Sleep Mult", config->getBattSleepMult());
  snprintf(labels[10], 22, "%-14s%5d", "Recovery N", config->getBattRecoveryN());
  snprintf(labels[11], 22, "%-14s%5s", "Debug", debugEnabled ? "ON" : "OFF");
  snprintf(labels[12], 22, "Back");

  display.clearDisplay();
  displayTitle();

  for (int i = 0; i < VISIBLE_ITEMS; i++) {
    int itemIdx = scrollOffset + i;
    int y = TRACK_Y + i * 9;
    if (itemIdx == selectedIndex) {
      display.fillRect(0, y - 1, SCROLLBAR_X, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    display.print(labels[itemIdx]);
  }

  // Scrollbar
  int thumbH = (VISIBLE_ITEMS * TRACK_H) / TOTAL_ITEMS;
  int maxOffset = TOTAL_ITEMS - VISIBLE_ITEMS;
  int thumbY = TRACK_Y + (scrollOffset * (TRACK_H - thumbH)) / maxOffset;
  display.fillRect(SCROLLBAR_X, thumbY, SCROLLBAR_W, thumbH, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showMenuEdit(const char* name, uint16_t value, uint16_t currentValue) {
  if (!enabled) {
    return;
  }

  display.clearDisplay();
  displayTitle();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 14);
  display.print(name);

  char valStr[6];
  snprintf(valStr, sizeof(valStr), "%d", value);
  display.setTextSize(3);
  int valWidth = strlen(valStr) * 18;
  display.setCursor((SCREEN_WIDTH - valWidth) / 2, 28);
  display.print(valStr);

  const char* action = (value == currentValue) ? "back" : "save";
  char hint[22];
  snprintf(hint, sizeof(hint), "now:%-3d  hold=%s", currentValue, action);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(hint);

  display.display();
}

void Display::showMenuCancel(const char* name, uint16_t currentValue) {
  if (!enabled) {
    return;
  }

  display.clearDisplay();
  displayTitle();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 14);
  display.print(name);

  display.setTextSize(2);
  int valWidth = 6 * 12;
  display.setCursor((SCREEN_WIDTH - valWidth) / 2, 32);
  display.print("CANCEL");

  char hint[22];
  snprintf(hint, sizeof(hint), "now:%-3d  hold=back", currentValue);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(hint);

  display.display();
}
