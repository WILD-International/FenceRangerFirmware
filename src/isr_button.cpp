
#include "isr_button.h"

#include <esp_timer.h>

static constexpr int64_t DEBOUNCE_US = 10000;

RTC_DATA_ATTR bool pressed = false;

static uint8_t buttonPin = 0;

static void IRAM_ATTR buttonISR() {
  pressed = true;
  digitalWrite(LED, HIGH);
}

void buttonSetup(uint8_t pin) {
  buttonPin = pin;
  pinMode(pin, INPUT_PULLUP);
  pressed = false;
  attachInterrupt(pin, buttonISR, CHANGE);
}

bool wasButtonPressed() {
  return pressed;
}

void buttonResetState() {
  pressed = false;
  digitalWrite(LED, LOW);
}
