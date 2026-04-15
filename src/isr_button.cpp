
#include "isr_button.h"

#include <esp_timer.h>

static constexpr int64_t DEBOUNCE_US = 10000;
static constexpr int64_t LONG_PRESS_US = 600000;

volatile bool shortPressed = false;
volatile bool longPressed = false;

static volatile int64_t lastEdgeTime = 0;
static volatile int64_t pressTime = 0;
static volatile bool wasPressed = false;
static uint8_t buttonPin = 0;

static void IRAM_ATTR buttonISR() {
  int64_t now = esp_timer_get_time();

  if (now - lastEdgeTime < DEBOUNCE_US)
    return;
  lastEdgeTime = now;

  bool pressed = !digitalRead(buttonPin);
  if (pressed) {
    digitalWrite(LED, HIGH);
  }

  if (pressed && !wasPressed) {
    pressTime = now;
    wasPressed = true;
  } else if (!pressed && wasPressed) {
    wasPressed = false;
    if (now - pressTime >= LONG_PRESS_US) {
      longPressed = true;
    } else {
      shortPressed = true;
    }
  }
}

void buttonSetup(uint8_t pin) {
  buttonPin = pin;
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(pin, buttonISR, CHANGE);
}

// --- Wake from deep sleep: blocking poll ---
void handleWakePress() {
  delayMicroseconds(DEBOUNCE_US);

  // if (digitalRead(buttonPin) == HIGH) {
  //   // already released — spurious
  //   return;
  // }

  int64_t start = esp_timer_get_time();
  while (!digitalRead(buttonPin)) {
    if (esp_timer_get_time() - start >= LONG_PRESS_US) {
      longPressed = true;
      return;
    }
    delayMicroseconds(1000);
  }
  shortPressed = true;
}
