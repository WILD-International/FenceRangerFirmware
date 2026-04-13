#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

namespace Debug {
bool enabled();
void enable();
void disable();
}  // namespace Debug

#define LOG(fmt, ...)                    \
  do {                                   \
    if (Debug::enabled())                \
      Serial.printf(fmt, ##__VA_ARGS__); \
  } while (0)
#define LOGLN(x)          \
  do {                    \
    if (Debug::enabled()) \
      Serial.println(x);  \
  } while (0)
#define LOGPRINT(x)       \
  do {                    \
    if (Debug::enabled()) \
      Serial.print(x);    \
  } while (0)

#endif  // DEBUG_H
