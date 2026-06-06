#ifndef LOGGING_H
#define LOGGING_H

#ifndef DISABLE_LOGGING
#include <Arduino.h>
#define LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#define LOGLN(x) Serial.println(x)
#define LOGPRINT(x) Serial.print(x)
#else
#define LOG(fmt, ...) \
  do {                \
  } while (0)
#define LOGLN(x) \
  do {           \
  } while (0)
#define LOGPRINT(x) \
  do {              \
  } while (0)
#endif

#endif  // LOGGING_H
