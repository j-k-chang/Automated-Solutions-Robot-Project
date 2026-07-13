#include "calibration_store.h"
#include <Arduino.h>
#include <string.h>

#if defined(ARDUINO_ARCH_MBED)
#include "kvstore_global_api.h"
#ifndef MBED_SUCCESS
#define MBED_SUCCESS 0
#endif
#define CAL_STORE_HAS_KV 1
#else
#define CAL_STORE_HAS_KV 0
#endif

namespace MultiPump {

static const char* CAL_KV_KEY = "/kv/asrp_cal_v1";
static const uint32_t CAL_MAGIC = 0x41535250UL; // 'ASRP'
static const uint16_t CAL_VERSION = 1;

struct CalibrationBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t pumpCount;
  float stepsPerGramLow[MAX_PUMP_COUNT];
  float stepsPerGramHigh[MAX_PUMP_COUNT];
  uint32_t checksum;
};

static uint32_t checksumBlob(const CalibrationBlob& blob) {
  uint32_t sum = 0;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
  size_t n = sizeof(CalibrationBlob) - sizeof(uint32_t);
  for (size_t i = 0; i < n; ++i) {
    sum = (sum * 131U) + bytes[i];
  }
  return sum;
}

static bool valuesLookSane(const float* values, int pumpCount) {
  for (int i = 0; i < pumpCount; ++i) {
    if (!(values[i] > 50.0f && values[i] < 20000.0f)) {
      return false;
    }
  }
  return true;
}

bool calibrationStoreLoad(float* stepsPerGramLow,
                          float* stepsPerGramHigh,
                          int pumpCount) {
  if (!stepsPerGramLow || !stepsPerGramHigh || pumpCount <= 0) {
    return false;
  }
  if (pumpCount > MAX_PUMP_COUNT) {
    pumpCount = MAX_PUMP_COUNT;
  }

#if CAL_STORE_HAS_KV
  CalibrationBlob blob;
  memset(&blob, 0, sizeof(blob));
  kv_info_t info;
  int rc = kv_get_info(CAL_KV_KEY, &info);
  if (rc != MBED_SUCCESS) {
    Serial.println("INFO:No saved calibration found (using defaults).");
    return false;
  }
  if (info.size != sizeof(CalibrationBlob)) {
    Serial.println("WARN:Calibration blob size mismatch; ignoring saved data.");
    return false;
  }

  size_t actual = 0;
  rc = kv_get(CAL_KV_KEY, &blob, sizeof(blob), &actual);
  if (rc != MBED_SUCCESS || actual != sizeof(blob)) {
    Serial.println("WARN:Failed to read calibration from KVStore.");
    return false;
  }
  if (blob.magic != CAL_MAGIC || blob.version != CAL_VERSION) {
    Serial.println("WARN:Calibration magic/version mismatch; ignoring saved data.");
    return false;
  }
  if (blob.checksum != checksumBlob(blob)) {
    Serial.println("WARN:Calibration checksum failed; ignoring saved data.");
    return false;
  }

  int count = (int)blob.pumpCount;
  if (count <= 0 || count > MAX_PUMP_COUNT) {
    count = pumpCount;
  }
  if (count > pumpCount) {
    count = pumpCount;
  }
  if (!valuesLookSane(blob.stepsPerGramLow, count) ||
      !valuesLookSane(blob.stepsPerGramHigh, count)) {
    Serial.println("WARN:Calibration values out of range; ignoring saved data.");
    return false;
  }

  memcpy(stepsPerGramLow, blob.stepsPerGramLow, sizeof(float) * (size_t)count);
  memcpy(stepsPerGramHigh, blob.stepsPerGramHigh, sizeof(float) * (size_t)count);
  Serial.print("INFO:Loaded calibration for ");
  Serial.print(count);
  Serial.println(" pump(s) from flash.");
  return true;
#else
  (void)stepsPerGramLow;
  (void)stepsPerGramHigh;
  (void)pumpCount;
  Serial.println("WARN:KVStore unavailable; calibration will not persist.");
  return false;
#endif
}

bool calibrationStoreSave(const float* stepsPerGramLow,
                          const float* stepsPerGramHigh,
                          int pumpCount) {
  if (!stepsPerGramLow || !stepsPerGramHigh || pumpCount <= 0) {
    return false;
  }
  if (pumpCount > MAX_PUMP_COUNT) {
    pumpCount = MAX_PUMP_COUNT;
  }
  if (!valuesLookSane(stepsPerGramLow, pumpCount) ||
      !valuesLookSane(stepsPerGramHigh, pumpCount)) {
    Serial.println("ERROR:Refusing to save calibration; values out of range.");
    return false;
  }

#if CAL_STORE_HAS_KV
  CalibrationBlob blob;
  memset(&blob, 0, sizeof(blob));
  blob.magic = CAL_MAGIC;
  blob.version = CAL_VERSION;
  blob.pumpCount = (uint16_t)pumpCount;
  memcpy(blob.stepsPerGramLow, stepsPerGramLow, sizeof(float) * (size_t)pumpCount);
  memcpy(blob.stepsPerGramHigh, stepsPerGramHigh, sizeof(float) * (size_t)pumpCount);
  for (int i = pumpCount; i < MAX_PUMP_COUNT; ++i) {
    blob.stepsPerGramLow[i] = stepsPerGramLow[0];
    blob.stepsPerGramHigh[i] = stepsPerGramHigh[0];
  }
  blob.checksum = checksumBlob(blob);

  int rc = kv_set(CAL_KV_KEY, &blob, sizeof(blob), 0);
  if (rc != MBED_SUCCESS) {
    Serial.print("ERROR:Failed to save calibration to KVStore (rc=");
    Serial.print(rc);
    Serial.println(").");
    return false;
  }
  Serial.println("INFO:Calibration saved to flash.");
  return true;
#else
  Serial.println("ERROR:KVStore unavailable; cannot save calibration.");
  return false;
#endif
}

void calibrationStorePrint(const float* stepsPerGramLow,
                           const float* stepsPerGramHigh,
                           int pumpCount) {
  if (!stepsPerGramLow || !stepsPerGramHigh || pumpCount <= 0) {
    return;
  }
  if (pumpCount > MAX_PUMP_COUNT) {
    pumpCount = MAX_PUMP_COUNT;
  }

  Serial.println("========================================");
  Serial.println("Calibration (steps/gram, full steps):");
  for (int i = 0; i < pumpCount; ++i) {
    Serial.print("  Pump ");
    Serial.print(i + 1);
    Serial.print("  Water=");
    Serial.print(stepsPerGramLow[i], 2);
    Serial.print("  Glycerol=");
    Serial.println(stepsPerGramHigh[i], 2);
  }
  Serial.println("========================================");
}

} // namespace MultiPump
