#ifndef SLEEP_H
#define SLEEP_H

#include <Arduino.h>

#include "config.h"

int64_t rtcTimeNowUs();
int64_t getTimeSleptMs();

void gpioWakeInit(Config& config);
void gpioSleepLatch(Config& config);
void gotoSleep(Config& config, uint32_t ms, bool setSleepTime = true);

#endif  // SLEEP_H
