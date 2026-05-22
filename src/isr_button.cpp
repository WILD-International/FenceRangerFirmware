
#include "isr_button.h"

#include "rtc_state.h"

static uint8_t buttonPin = 0;

static void IRAM_ATTR buttonISR() {
  rtc.buttonPressed = true;
  digitalWrite(LED, HIGH);
}

void buttonSetup(uint8_t pin) {
  buttonPin = pin;
  pinMode(pin, INPUT_PULLUP);
  rtc.buttonPressed = false;
  attachInterrupt(pin, buttonISR, CHANGE);
}

bool wasButtonPressed() {
  return rtc.buttonPressed;
}

void buttonResetState() {
  rtc.buttonPressed = false;
  digitalWrite(LED, LOW);
}
