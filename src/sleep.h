#ifndef SLEEP_H
#define SLEEP_H

#include <Arduino.h>

int64_t rtcTimeNowUs();
int64_t getTimeSleptMs();

void gpioWakeInit();
void gpioSleepLatch();
void gotoSleep(uint32_t ms, bool setSleepTime = true);

#endif  // SLEEP_H
