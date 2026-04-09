#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

enum class ButtonEvent { None, ShortPress, LongPress };

class Button {
 public:
  void begin(uint8_t pin, uint16_t longPressMs = 600) {
    pin_ = pin;
    longPressMs_ = longPressMs;
    pinMode(pin_, INPUT_PULLUP);
  }

  ButtonEvent update() {
    bool pressed = digitalRead(pin_) == LOW;
    ButtonEvent event = ButtonEvent::None;

    if (pressed && !wasPressed_) {
      // just pressed down
      pressStart_ = millis();
      held_ = false;
    }

    if (pressed && !held_ && (millis() - pressStart_ >= longPressMs_)) {
      // held long enough
      event = ButtonEvent::LongPress;
      held_ = true;
    }

    if (!pressed && wasPressed_) {
      // just released
      if (!held_) {
        event = ButtonEvent::ShortPress;
      }
    }

    wasPressed_ = pressed;
    return event;
  }

 private:
  uint8_t pin_;
  uint16_t longPressMs_;
  unsigned long pressStart_ = 0;
  bool wasPressed_ = false;
  bool held_ = false;
};

#endif  // BUTTON_H
