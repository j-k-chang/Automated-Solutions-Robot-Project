#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H

#include "config.h"
#include <stdint.h>

namespace MultiPump {

// Persist per-pump steps/gram (low + high viscosity profiles) across reboot.
// Uses Mbed KVStore on the Giga R1 (no classic EEPROM).

bool calibrationStoreLoad(float* stepsPerGramLow,
                          float* stepsPerGramHigh,
                          int pumpCount);

bool calibrationStoreSave(const float* stepsPerGramLow,
                          const float* stepsPerGramHigh,
                          int pumpCount);

void calibrationStorePrint(const float* stepsPerGramLow,
                           const float* stepsPerGramHigh,
                           int pumpCount);

} // namespace MultiPump

#endif // CALIBRATION_STORE_H
