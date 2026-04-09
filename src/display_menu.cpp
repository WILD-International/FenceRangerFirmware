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

  changeState(DISPLAY_MENU_LIVE_INFO);
}

void DisplayMenu::update(State& state) {
  switch (currentMenu) {
    case DISPLAY_MENU_OFF:
      // do nothing
      break;
    case DISPLAY_MENU_LIVE_INFO:
      if (millis() - last_button_event > 5 * 60 * 1000) {
        changeState(DISPLAY_MENU_OFF);
      }
      break;
  }
}

void DisplayMenu::buttonEvent(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::ShortPress:
      last_button_event = millis();
      if (currentMenu == DISPLAY_MENU_OFF) {
        changeState(DISPLAY_MENU_LIVE_INFO);
      } else if (currentMenu == DISPLAY_MENU_LIVE_INFO) {
        changeState(DISPLAY_MENU_OFF);
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
  switch (newState) {
    case DISPLAY_MENU_OFF:
      Serial.println("Turning off display");
      Wire.end();
      digitalWrite(DISPLAY_POWER, HIGH);
      break;
    case DISPLAY_MENU_LIVE_INFO:
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
        return;
      }
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("WILD FenceRanger");
      display.setCursor(0, 10);
      display.println("Name:");
      display.println(config->getDevName());
      display.setCursor(0, 30);
      display.println("Device EUI:");
      char hex[DEVEUI_LEN * 2 + 1] = {};
      bytesToHex(config->getDevEUI(), DEVEUI_LEN, hex);
      display.println(hex);
      display.display();
      break;
  }
  currentMenu = newState;
}
