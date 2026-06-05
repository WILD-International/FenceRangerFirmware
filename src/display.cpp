#include "display.h"

#include "board.h"
#include "esp32-hal-gpio.h"
#include "helpers.h"
#include "version.h"

const char* title = "WILD FenceRanger";

static void printTruncated(Adafruit_SSD1306& disp, const char* text, int maxChars) {
  if ((int)strlen(text) <= maxChars) {
    disp.print(text);
    return;
  }
  char buf[22];
  strncpy(buf, text, maxChars - 1);
  buf[maxChars - 1] = '\0';
  disp.print(buf);
  int16_t x = disp.getCursorX();
  int16_t y = disp.getCursorY() + 6;
  disp.drawPixel(x, y, SSD1306_WHITE);
  disp.drawPixel(x + 2, y, SSD1306_WHITE);
  disp.drawPixel(x + 4, y, SSD1306_WHITE);
}

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

void Display::messageWithCode(const char* msg, int code, uint32_t delayMs) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayTitle();
  display.setTextSize(2);
  display.setCursor((SCREEN_WIDTH - strlen(msg) * 12) / 2, 16);
  display.println(msg);
  display.setTextSize(1);
  char codeStr[16];
  snprintf(codeStr, sizeof(codeStr), "code: %d", code);
  display.setCursor((SCREEN_WIDTH - (int)strlen(codeStr) * 6) / 2, 40);
  display.print(codeStr);
  display.display();
  if (delayMs > 0) {
    delay(delayMs);
  }
}

void Display::showMeasurement(const Measurement& measurement, uint32_t delayMs) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayTitle();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 11);
  display.printf("Fence+ %5u V", measurement.fencePosMaxV);
  display.setCursor(2, 22);
  display.printf("Fence- %5u V", measurement.fenceNegMaxV);
  display.setCursor(2, 33);
  display.printf("Batt   %5u mV", measurement.battMaxMv);
  if (measurement.adcErrorCount > 0) {
    display.setCursor(2, 44);
    display.printf("Errors %5u", measurement.adcErrorCount);
  }
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
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* name = config->getDevName();
  int nameLen = strlen(name);
  if (nameLen <= 15) {
    display.setCursor(2, 12);
    display.print("Name: ");
    display.print(name);
  } else if (nameLen <= 21) {
    display.setCursor(2, 12);
    display.print("Name:");
    display.setCursor(SCREEN_WIDTH - nameLen * 6, 22);
    display.print(name);
  } else {
    char line1[22];
    strncpy(line1, name, 21);
    line1[21] = '\0';
    display.setCursor(2, 12);
    display.print(line1);
    display.setCursor(2, 22);
    printTruncated(display, name + 21, 21);
  }

  display.setCursor(2, 32);
  display.print("EUI: ");
  bytesToHex(config->getDevEUI(), DEVEUI_LEN, hex);
  display.print(hex);

  display.setCursor(2, 42);
  display.print("ver: ");
  printTruncated(display, GIT_TAG, 16);

  display.fillRect(0, 55, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 56);
  display.print("tap=exit  hold=menu");
  display.setTextColor(SSD1306_WHITE);

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

void Display::displayHeader(const char* text) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor((SCREEN_WIDTH - strlen(text) * 6) / 2, 2);
  display.println(text);
}

void Display::displayTitle() {
  displayHeader(title);
}

void Display::showMenu(int selectedIndex) {
  if (!enabled) {
    return;
  }

  const int TOTAL_ITEMS = 8;
  const int VISIBLE_ITEMS = 6;
  const int SCROLLBAR_X = 124;
  const int SCROLLBAR_W = 4;
  const int TRACK_Y = 11;
  const int TRACK_H = 53;

  int scrollOffset = (selectedIndex >= VISIBLE_ITEMS) ? selectedIndex - VISIBLE_ITEMS + 1 : 0;

  char labels[TOTAL_ITEMS][22];
  snprintf(labels[0], 22, "%-14s%5s", "Alert config", ">");
  snprintf(labels[1], 22, "%-14s%5s", "Sleep config", ">");
  snprintf(labels[2], 22, "%-14s%5s", "Measure config", ">");
  snprintf(labels[3], 22, "%-14s%5s", "Radio config", ">");
  snprintf(labels[4], 22, "%-14s%5s", "Voltage Test", ">");
  snprintf(labels[5], 22, "%-14s%5s", "LoRa Test", ">");
  snprintf(labels[6], 22, "%-14s%5s", "WiFi AP", ">");
  snprintf(labels[7], 22, "Back");

  display.clearDisplay();
  displayHeader("Menu");

  for (int i = 0; i < VISIBLE_ITEMS; i++) {
    int itemIdx = scrollOffset + i;
    if (itemIdx >= TOTAL_ITEMS) {
      break;
    }
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

  int thumbH = (VISIBLE_ITEMS * TRACK_H) / TOTAL_ITEMS;
  int maxOffset = TOTAL_ITEMS - VISIBLE_ITEMS;
  int thumbY = TRACK_Y + (scrollOffset * (TRACK_H - thumbH)) / maxOffset;
  display.fillRect(SCROLLBAR_X, thumbY, SCROLLBAR_W, thumbH, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showSubMenu(const char* header, const char labels[][SUBMENU_LABEL_LEN],
                          int totalItems, int selectedIndex, int scrollOffset) {
  if (!enabled) {
    return;
  }

  const int VISIBLE_ITEMS = 6;
  const int SCROLLBAR_X = 124;
  const int SCROLLBAR_W = 4;
  const int TRACK_Y = 11;
  const int TRACK_H = 53;

  display.clearDisplay();
  displayHeader(header);

  for (int i = 0; i < VISIBLE_ITEMS; i++) {
    int itemIdx = scrollOffset + i;
    if (itemIdx >= totalItems) {
      break;
    }
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

  if (totalItems > VISIBLE_ITEMS) {
    int thumbH = (VISIBLE_ITEMS * TRACK_H) / totalItems;
    int maxOffset = totalItems - VISIBLE_ITEMS;
    int thumbY = TRACK_Y + (scrollOffset * (TRACK_H - thumbH)) / maxOffset;
    display.fillRect(SCROLLBAR_X, thumbY, SCROLLBAR_W, thumbH, SSD1306_WHITE);
  }

  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showMenuEdit(const char* name, uint16_t value, uint16_t currentValue) {
  if (!enabled) {
    return;
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 4);
  display.print(name);

  char valStr[6];
  snprintf(valStr, sizeof(valStr), "%d", value);
  display.setTextSize(3);
  int valWidth = strlen(valStr) * 18;
  display.setCursor((SCREEN_WIDTH - valWidth) / 2, 18);
  display.print(valStr);

  const char* action = (value == currentValue) ? "back" : "save";
  char hint[22];
  snprintf(hint, sizeof(hint), "now:%-3d  hold=%s", currentValue, action);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(hint);

  display.display();
}

void Display::showVoltageTest(const Measurement& measurement) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayHeader("Volt Test");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 11);
  display.printf("Fence+ %5u V", measurement.fencePosMaxV);
  display.setCursor(2, 22);
  display.printf("Fence- %5u V", measurement.fenceNegMaxV);
  display.setCursor(2, 33);
  display.printf("Batt   %5u mV", measurement.battMaxMv);
  if (measurement.adcErrorCount > 0) {
    display.setCursor(2, 44);
    display.printf("Errors %5u", measurement.adcErrorCount);
  }
  display.fillRect(0, 55, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 56);
  display.print("tap=stop");
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showLoraTest(bool joined, LoraTestState state, const SignalInfo* signalInfo) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayHeader("LoRa Test");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (state == LoraTestState::Received && signalInfo != nullptr) {
    display.setCursor(2, 13);
    display.printf("RSSI: %.0f dBm", signalInfo->rssi);
    display.setCursor(2, 23);
    display.printf("SNR:  %.1f dB", signalInfo->snr);
    display.setCursor(2, 33);
    display.printf("Freq: %.1f MHz", signalInfo->freq);
    display.setCursor(2, 43);
    display.printf("DR: %d   RX win: %d", signalInfo->datarate, signalInfo->rxWindow);
  } else if (state == LoraTestState::NoDownlink) {
    const char* noDownlinkMsg = "Sent, no downlink";
    display.setCursor((SCREEN_WIDTH - (int)strlen(noDownlinkMsg) * 6) / 2, 24);
    display.print(noDownlinkMsg);
  } else {
    const char* status = joined ? "Joined" : "Not joined";
    display.setCursor((SCREEN_WIDTH - (int)strlen(status) * 6) / 2, 24);
    display.print(status);
  }

  display.fillRect(0, 55, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 56);
  display.print(joined ? "tap=send  hold=back" : "tap=join  hold=back");
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showWifiAp(const char* ssid, const char* password) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayHeader("WiFi AP");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 13);
  display.print("SSID:");
  display.setCursor(2, 23);
  printTruncated(display, ssid, 21);
  display.setCursor(2, 36);
  display.print("Pass:");
  display.setCursor(2, 46);
  printTruncated(display, password, 21);
  display.fillRect(0, 55, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 56);
  display.print("hold=back");
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showWifiStation(const char* ssid, const char* status, const char* ip) {
  if (!enabled) {
    return;
  }
  display.clearDisplay();
  displayHeader("WiFi STA");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 13);
  display.print("SSID:");
  display.setCursor(2, 23);
  printTruncated(display, ssid, 21);
  display.setCursor(2, 36);
  printTruncated(display, status, 21);
  display.setCursor(2, 46);
  printTruncated(display, ip, 21);
  display.fillRect(0, 55, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 56);
  display.print("hold=back");
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void Display::showMenuCancel(const char* name, uint16_t currentValue) {
  if (!enabled) {
    return;
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 4);
  display.print(name);

  display.setTextSize(2);
  int valWidth = 6 * 12;
  display.setCursor((SCREEN_WIDTH - valWidth) / 2, 24);
  display.print("CANCEL");

  char hint[22];
  snprintf(hint, sizeof(hint), "now:%-3d  hold=back", currentValue);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(hint);

  display.display();
}
