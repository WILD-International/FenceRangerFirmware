#ifndef ISR_BUTTON_H
#define ISR_BUTTON_H

#include <Arduino.h>

void buttonSetup(uint8_t pin);

bool wasButtonPressed();
void buttonResetState();

#endif  // ISR_BUTTON_H
