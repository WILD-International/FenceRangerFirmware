#ifndef ISR_BUTTON_H
#define ISR_BUTTON_H

#include <Arduino.h>

extern volatile bool shortPressed;
extern volatile bool longPressed;

void buttonSetup(uint8_t pin);
void handleWakePress();

#endif  // ISR_BUTTON_H
