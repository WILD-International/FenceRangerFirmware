#ifndef SLEEP_H
#define SLEEP_H

#include <Arduino.h>

int64_t rtcTimeNowUs();
int64_t getTimeSleptMs();

void gpioWakeInit();
void gpioSleepLatch();
void gotoSleep(uint32_t ms, bool setSleepTime = true);

// Selects the PA-enable pin (GPIO 2) deep-sleep state: 0=LOW, 1=HIGH, 2=FLOAT.
void configurePaSleepPin(uint8_t mode);

#endif  // SLEEP_H
