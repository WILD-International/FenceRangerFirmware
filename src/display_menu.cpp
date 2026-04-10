#include "display_menu.h"

#include "board.h"
#include "esp32-hal-gpio.h"
#include "helpers.h"

DisplayMenu::DisplayMenu(Config* config) : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST) {
  this->config = config;
}
DisplayMenu::~DisplayMenu() {}

void DisplayMenu::begin() {
  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, HIGH);  // power off

  changeState(DISPLAY_MENU_DEVICE_INFO);
}

void DisplayMenu::update(State& state) {
  uint32_t now = millis();
  if (now - last_display_update < 200) {
    return;
  }
  last_display_update = now;

  char hex[DEVEUI_LEN * 2 + 1] = {};

  switch (currentMenu) {
    case DISPLAY_MENU_OFF:
      // do nothing
      break;
    case DISPLAY_MENU_DEVICE_INFO:
      if (millis() - last_button_event > 5 * 60 * 1000) {
        changeState(DISPLAY_MENU_OFF);
      }
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(16, 2);
      display.println("WILD FenceRanger");
      display.setCursor(0, 14);
      display.println("Name:");
      display.println(config->getDevName());
      display.setCursor(0, 34);
      display.println("Device EUI:");
      bytesToHex(config->getDevEUI(), DEVEUI_LEN, hex);
      display.println(hex);
      display.setCursor(0, 54);
      display.print("ver: ");
      display.print(GIT_TAG);
      display.display();
      break;
    case DISPLAY_MENU_LIVE_INFO:
      if (millis() - last_button_event > 5 * 60 * 1000) {
        changeState(DISPLAY_MENU_OFF);
        break;
      }
      if (state.isMeasurementRunning()) {
        break;  // don't update display if we're measuring
      }
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(16, 2);
      display.println("WILD FenceRanger");
      display.setCursor(0, 16);
      display.printf("batt: %d %d", state.getBattMinMv(), state.getBattMaxMv());
      display.setCursor(0, 28);
      display.printf(" neg: %d %d", state.getFenceNegMinMv(), state.getFenceNegMaxMv());
      display.setCursor(0, 40);
      display.printf(" pos: %d %d", state.getFencePosMinMv(), state.getFencePosMaxMv());
      display.setCursor(0, 52);
      display.printf(" err: %d", state.getAdcErrorCount());
      //
      display.display();
      break;
  }
}

void DisplayMenu::buttonEvent(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::ShortPress:
      last_button_event = millis();
      switch (currentMenu) {
        case DISPLAY_MENU_OFF:
          changeState(DISPLAY_MENU_DEVICE_INFO);
          break;
        case DISPLAY_MENU_DEVICE_INFO:
          changeState(DISPLAY_MENU_LIVE_INFO);
          break;
        case DISPLAY_MENU_LIVE_INFO:
          changeState(DISPLAY_MENU_OFF);
          break;
      }
      break;

    case ButtonEvent::LongPress:
      last_button_event = millis();
      break;

    case ButtonEvent::None:
      // fall through
    default:
      break;
  }
}

void DisplayMenu::changeState(DisplayMenuState newState) {
  if (newState == currentMenu) {
    return;
  }
  char hex[DEVEUI_LEN * 2 + 1] = {};
  switch (newState) {
    case DISPLAY_MENU_OFF:
      Serial.println("Turning off display");
      Wire.end();
      digitalWrite(DISPLAY_POWER, HIGH);
      break;
    case DISPLAY_MENU_DEVICE_INFO:
      Serial.println("Turning on display");
      // Turn on the display and initialize
      digitalWrite(DISPLAY_POWER, LOW);  // power on the display
      // Start I2C on the correct pins
      Wire.begin(OLED_SDA, OLED_SCL);

      // Initialize display (0x3C is the typical I2C address)
      if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 init failed");
        Wire.end();
        digitalWrite(DISPLAY_POWER, HIGH);
        return;  // don't change state if init failed
      }
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(16, 2);
      display.println("WILD FenceRanger");
      display.setCursor(0, 14);
      display.println("Name:");
      display.println(config->getDevName());
      display.setCursor(0, 34);
      display.println("Device EUI:");
      bytesToHex(config->getDevEUI(), DEVEUI_LEN, hex);
      display.println(hex);
      display.setCursor(0, 54);
      display.print("ver: ");
      display.print(GIT_TAG);
      display.display();
      break;
    case DISPLAY_MENU_LIVE_INFO:
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(16, 2);
      display.println("WILD FenceRanger");
      display.setCursor(0, 16);
      display.println("Measuring...");
      display.display();
      break;
  }
  currentMenu = newState;
}
