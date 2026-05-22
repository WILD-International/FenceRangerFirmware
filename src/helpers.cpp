#include "helpers.h"

#include <Arduino.h>

void bytesToHex(const uint8_t* data, size_t len, char* out) {
  for (size_t i = 0; i < len; i++) {
    sprintf(out + i * 2, "%02X", data[i]);
  }
}

bool hexToBytes(const char* hex, uint8_t* out, size_t expectedLen) {
  size_t hexLen = strlen(hex);
  if (hexLen != expectedLen * 2)
    return false;
  for (size_t i = 0; i < expectedLen; i++) {
    char byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char* end;
    unsigned long val = strtoul(byte, &end, 16);
    if (*end != '\0')
      return false;
    out[i] = (uint8_t)val;
  }
  return true;
}
