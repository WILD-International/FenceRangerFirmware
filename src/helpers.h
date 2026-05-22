#ifndef HELPERS_H
#define HELPERS_H

#include <Arduino.h>

int64_t rtcTimeNowUs();
void gotoSleep(uint32_t ms, bool setSleepTime = true);
int64_t getTimeSleptMs();
void bytesToHex(const uint8_t* data, size_t len, char* out);
bool hexToBytes(const char* hex, uint8_t* out, size_t expectedLen);

#endif  // HELPERS_H
