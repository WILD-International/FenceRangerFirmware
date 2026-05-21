#ifndef ALERT_H
#define ALERT_H

#include <Arduino.h>

bool processMeasurement(uint16_t fenceVoltage, uint16_t alertThresholdV,
                        uint16_t recoverThresholdV);

#endif  // ALERT_H
