#include "Multipump_dispensing.h"
#include "config.h"
#include "Mixer.h"
#include "calibration_store.h"

namespace MultiPump {

// --- Helper: case-insensitive string comparison ---
static bool strEqualsIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    if (ca != cb) return false;
    a++; b++;
  }
  return (*a == '\0' && *b == '\0');
}

// --- Helper: case-insensitive prefix check ---
static bool strStartsWithIgnoreCase(const char* str, const char* prefix) {
  while (*prefix) {
    char cs = (*str >= 'A' && *str <= 'Z') ? (*str + 32) : *str;
    char cp = (*prefix >= 'A' && *prefix <= 'Z') ? (*prefix + 32) : *prefix;
    if (cs != cp) return false;
    str++; prefix++;
  }
  return true;
}

// --- Helper: trim whitespace in-place, return length ---
static int strTrim(char* s) {
  char* start = s;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  if (s != start) {
    char* d = start;
    while (*s) *d++ = *s++;
    *d = '\0';
    return (int)(d - start);
  }
  char* end = s + strlen(s) - 1;
  while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    *end = '\0'; end--;
  }
  return (int)strlen(start);
}

Mixer mixer(52, 48, 50);

// --- Dispensing Configuration ---
const float BULK_SPEED = 10000.0f;
const float TRICKLE_SPEED = 12000.0f;
const long retractStepsWater = 3200;
const long retractStepsGlycerol = 9600;
const float DISPENSE_TOLERANCE_G = 0.01f;
const unsigned long HIGH_VISC_SETTLE_GUARD_MS = 3000;
const long HIGH_VISC_RELIEF_FAST_STEPS = 2400;
const long HIGH_VISC_RELIEF_FINISH_STEPS = 7200;
const float HIGH_VISC_RELIEF_FAST_SPEED = -16000.0f;
const float HIGH_VISC_RELIEF_FINISH_SPEED = -8000.0f;

// --- Pump Hardware ---
AccelStepper pump1(AccelStepper::DRIVER, PUMP1_STEP, SHARED_DIR);
AccelStepper pump2(AccelStepper::DRIVER, PUMP2_STEP, SHARED_DIR);
AccelStepper pump3(AccelStepper::DRIVER, PUMP3_STEP, SHARED_DIR);
AccelStepper pump4(AccelStepper::DRIVER, PUMP4_STEP, SHARED_DIR);
AccelStepper pump5(AccelStepper::DRIVER, PUMP5_STEP, SHARED_DIR);
AccelStepper pump6(AccelStepper::DRIVER, PUMP6_STEP, SHARED_DIR);
AccelStepper pump7(AccelStepper::DRIVER, PUMP7_STEP, SHARED_DIR);
AccelStepper pump8(AccelStepper::DRIVER, PUMP8_STEP, SHARED_DIR);
AccelStepper pump9(AccelStepper::DRIVER, PUMP9_STEP, SHARED_DIR);
AccelStepper pump10(AccelStepper::DRIVER, PUMP10_STEP, SHARED_DIR);
AccelStepper pump11(AccelStepper::DRIVER, PUMP11_STEP, SHARED_DIR);
AccelStepper pump12(AccelStepper::DRIVER, PUMP12_STEP, SHARED_DIR);
AccelStepper pump13(AccelStepper::DRIVER, PUMP13_STEP, SHARED_DIR);
AccelStepper pump14(AccelStepper::DRIVER, PUMP14_STEP, SHARED_DIR);
AccelStepper pump15(AccelStepper::DRIVER, PUMP15_STEP, SHARED_DIR);
AccelStepper pump16(AccelStepper::DRIVER, PUMP16_STEP, SHARED_DIR);

AccelStepper* const pumps[MAX_PUMP_COUNT] = {
  &pump1, &pump2, &pump3, &pump4, &pump5, &pump6, &pump7,
  &pump8, &pump9, &pump10, &pump11, &pump12, &pump13, &pump14, &pump15, &pump16
};

const int pumpStepPins[MAX_PUMP_COUNT] = {
  PUMP1_STEP, PUMP2_STEP, PUMP3_STEP, PUMP4_STEP, PUMP5_STEP, PUMP6_STEP, PUMP7_STEP,
  PUMP8_STEP, PUMP9_STEP, PUMP10_STEP, PUMP11_STEP, PUMP12_STEP, PUMP13_STEP, PUMP14_STEP, PUMP15_STEP, PUMP16_STEP
};

void processScaleData(const char* raw);
void handleUsbCommands();

// --- Global State ---
float currentWeight = 0.0f;
float rawWeight = 0.0f;
float tareOffset = 0.0f;
float startWeight = 0.0f;
unsigned long lastScaleUpdateTime = 0;
unsigned long settleTimer = 0;
unsigned long stopTime = 0;
float lastSettleWeight = 0.0f;
bool newScaleData = false;

// Fill-rate tracking for predictive weight stop
float lastWeightForRate = 0.0f;
unsigned long lastWeightRateTime = 0;
float fillRateGps = 0.0f;

// Adaptive trim-pulse gain: measured (actual delivered / commanded) per pump.
// Separate banks for low- and high-viscosity profiles (same pattern as steps/gram).
// Learned in RAM until reboot; never shared across pumps.
float trimGainLow[MAX_PUMP_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
float trimGainHigh[MAX_PUMP_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
float bulkStopDelivered = 0.0f;
long bulkStopStepsMoved = 0;
float lastTrimPulseSizeG = 0.0f;
float trimPulseCommandedG = 0.0f;
float trimPulseStartDelivered = 0.0f;
bool trimPulseRanFull = false;

// In-flight mass accounting: grams pumped out but not yet registered on the
// scale (liquid clinging to the nozzle as a forming drop). Prevents stacking
// repeated pulses while waiting for a hanging drop to detach.
float trimBacklogG = 0.0f;
float trimBacklogAtPulseStart = 0.0f;
unsigned long trimBacklogWaitStart = 0;
float trimBacklogBaselineDelivered = 0.0f;
int trimBacklogWaitCycles = 0;
int trimPulseCount = 0;
int trimCatchupPulseCount = 0;
int trimCoarsePulseCount = 0;
bool trimCatchupPhase = false;
bool trimLastPulseCountedCoarse = false;
bool trimShrinkNextPulse = false;
// Near-target land gate: after a pulse with remaining ≤ TRIM_NEAR_BAND_G, wait for
// Δm ≥ DROP_CAL_DETECT_G (or hang timeout) before allowing another pulse.
bool trimNearLandGateActive = false;
float trimNearLandBaseline = 0.0f;
unsigned long trimNearLandWaitStart = 0;
int trimNearLandWaitCycles = 0;
bool trimInchwormActive = false;
int trimInchwormBurstCount = 0;
bool trimForceMassPulse = false;

// Drop characterization (steps/drop and g/drop)
static int dropCalPumpIndex = 0;
static int dropCalTargetCount = DROP_CAL_DEFAULT_COUNT;
static int dropCalRecorded = 0;
static long dropCalStepsSinceLast = 0;
static long dropCalTotalSteps = 0;
static float dropCalLastWeight = 0.0f;   // Baseline after last accepted/rejected event
static float dropCalPrevSample = 0.0f;   // Prior scale sample (motion check)
static float dropCalMasses[DROP_CAL_MAX_EVENTS];
static long dropCalStepsArr[DROP_CAL_MAX_EVENTS];
static unsigned long dropCalSettleStart = 0;
static bool dropCalInBurst = false;

static char scaleBuffer[64];
static int scaleBufIdx = 0;
static char usbBuffer[64];
static int usbBufIdx = 0;
const unsigned int MAX_BUF = 50;

SequenceState sequenceState = SEQ_PROMPT_TARGET;
DispenseState dispenseState = DISPENSE_IDLE;
static bool errorLocked = false;

int targetEntryPumpIndex = 0;
int activePumpIndex = -1;
int calibratingPumpIndex = 0;
int nextPumpIndex = -1;
bool promptedTarget = false;
bool completionAnnounced = false;

float targetWeights[MAX_PUMP_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
int dispenseOrder[MAX_PUMP_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
uint32_t targetReceivedMask = 0;

float stepsPerGramLow[MAX_PUMP_COUNT] = {700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f};
float stepsPerGramHigh[MAX_PUMP_COUNT] = {1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f};

bool highViscosity[MAX_PUMP_COUNT] = {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
float pumpViscosity[MAX_PUMP_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

static float getTrimGain(int pumpIndex) {
  if (pumpIndex < 0 || pumpIndex >= MAX_PUMP_COUNT) return 1.0f;
  return highViscosity[pumpIndex] ? trimGainHigh[pumpIndex] : trimGainLow[pumpIndex];
}

static void blendTrimGain(int pumpIndex, float measured, float alpha) {
  if (pumpIndex < 0 || pumpIndex >= MAX_PUMP_COUNT) return;
  float& gain = highViscosity[pumpIndex] ? trimGainHigh[pumpIndex] : trimGainLow[pumpIndex];
  gain = ((1.0f - alpha) * gain) + (alpha * measured);
}

// Forward declarations needed for maintenance helpers (declared before definitions below).
static int pumpDirSign(int pumpIndex);
float getViscosityFactor(int pumpIndex);
float getStepsPerGram(int pumpIndex);
float getStopLeadG(int pumpIndex, bool isTrimPhase);
int getPrimeDurationSec(float viscCps);
void setMicrostepping(int resolution);
extern int activeMicrosteps;

// --- Maintenance Sequence State ---
unsigned long maintenancePumpEndTime[MAX_PUMP_COUNT] = {0};
bool maintenancePumpActive[MAX_PUMP_COUNT] = {false};
float maintenanceSpeed[MAX_PUMP_COUNT] = {0.0f};

// Maintenance runs pumps in 2 phases to keep shared DIR coherent:
//  - Phase 1: odd-orientation group (pumps 1/3/5/7 => pumpDirSign == +1)
//  - Phase 2: even-orientation group (pumps 2/4/6/8 => pumpDirSign == -1)
static uint32_t maintenancePendingOddMask = 0;
static uint32_t maintenancePendingEvenMask = 0;
static bool maintenanceModePrime = false; // true=PRIME/PURGE, false=FLUSH
static int maintenanceDir = 1;            // PRIME dir argument (+1 fill, -1 purge)
static int maintenanceSeconds = 0;        // 0 means "auto" (PRIME only)
static float maintenanceSpeedOverride = 0.0f; // <=0 means "auto"

static uint32_t getOddGroupMask() {
  uint32_t mask = 0;
  for (int i = 0; i < PUMP_COUNT; ++i) {
    if (pumpDirSign(i) > 0) mask |= (1UL << i);
  }
  return mask;
}

static void clearMaintenanceArrays() {
  for (int i = 0; i < MAX_PUMP_COUNT; ++i) {
    maintenancePumpActive[i] = false;
    maintenanceSpeed[i] = 0.0f;
    maintenancePumpEndTime[i] = 0;
  }
}

static void startMaintenancePhase(uint32_t phaseMask, unsigned long now) {
  if (phaseMask == 0) return;

  bool anyPump = false;
  clearMaintenanceArrays();

  if (maintenanceModePrime) {
    // PRIME/PURGE: per-pump duration (seconds==0 => auto by viscosity)
    for (int i = 0; i < PUMP_COUNT; ++i) {
      if ((phaseMask & (1UL << i)) == 0 || pumpStepPins[i] == -1) continue;

      int pumpSeconds = maintenanceSeconds;
      if (pumpSeconds <= 0) {
        pumpSeconds = getPrimeDurationSec(pumpViscosity[i]);
      }

      float pumpSpeed = maintenanceSpeedOverride;
      if (pumpSpeed <= 0.0f) {
        pumpSpeed = (pumpViscosity[i] >= 100.0f) ? 6000.0f : 12000.0f;
      }

      maintenancePumpActive[i] = true;
      maintenancePumpEndTime[i] = now + ((unsigned long)pumpSeconds * 1000UL);
      // Prime fills the line in the same direction as dispense.
      maintenanceSpeed[i] = pumpSpeed * (float)maintenanceDir;
      anyPump = true;
    }
  } else {
    // FLUSH: single end time for all pumps in phase.
    unsigned long flushEndTime = now + ((unsigned long)maintenanceSeconds * 1000UL);
    for (int i = 0; i < PUMP_COUNT; ++i) {
      if ((phaseMask & (1UL << i)) == 0 || pumpStepPins[i] == -1) continue;

      float pumpSpeed = maintenanceSpeedOverride;
      if (pumpSpeed <= 0.0f) {
        pumpSpeed = (pumpViscosity[i] >= 100.0f) ? 7000.0f : 14000.0f;
      }

      maintenancePumpActive[i] = true;
      maintenancePumpEndTime[i] = flushEndTime;
      // Flush returns fluid to containers (opposite of dispense/prime).
      maintenanceSpeed[i] = -pumpSpeed;
      anyPump = true;
    }
  }

  if (!anyPump) {
    return;
  }

  activeMicrosteps = 16;
  setMicrostepping(activeMicrosteps);
  digitalWrite(SHARED_EN, LOW);
  delayMicroseconds(5);
  sequenceState = SEQ_MAINTENANCE_RUN;
}

static bool startNextMaintenancePhase(unsigned long now) {
  if (maintenancePendingOddMask != 0) {
    uint32_t phase = maintenancePendingOddMask;
    maintenancePendingOddMask = 0;
    Serial.println("INFO:Maintenance phase 1/2 (odd group)...");
    startMaintenancePhase(phase, now);
    return true;
  }
  if (maintenancePendingEvenMask != 0) {
    uint32_t phase = maintenancePendingEvenMask;
    maintenancePendingEvenMask = 0;
    Serial.println("INFO:Maintenance phase 2/2 (even group)...");
    startMaintenancePhase(phase, now);
    return true;
  }
  return false;
}

float getViscosityFactor(int pumpIndex) {
  if (pumpIndex < 0 || pumpIndex >= MAX_PUMP_COUNT) return 1.0f;
  float visc = pumpViscosity[pumpIndex];
  if (visc < 1.0f) visc = 1.0f;
  return log10(visc + 9.0f);
}

int getPrimeDurationSec(float viscCps) {
  if (viscCps < 1.0f) viscCps = 1.0f;
  float factor = log10(viscCps + 9.0f);
  float factorLow = log10(PRIME_REF_LOW_VISC_CPS + 9.0f);
  float factorHigh = log10(PRIME_REF_HIGH_VISC_CPS + 9.0f);
  float span = factorHigh - factorLow;
  if (span <= 0.0f) span = 1.0f;
  float duration = (float)LOW_VISC_PRIME_TIME_SEC +
    ((float)HIGH_VISC_PRIME_TIME_SEC - (float)LOW_VISC_PRIME_TIME_SEC) *
    (factor - factorLow) / span;
  if (duration < (float)LOW_VISC_PRIME_TIME_SEC) duration = (float)LOW_VISC_PRIME_TIME_SEC;
  if (duration > (float)HIGH_VISC_PRIME_TIME_SEC) duration = (float)HIGH_VISC_PRIME_TIME_SEC;
  return (int)(duration + 0.5f);
}

float getDynamicBulkFraction(int pumpIndex, float targetG) {
  if (targetG <= 0.0f) return 0.85f;
  float factor = getViscosityFactor(pumpIndex);
  bool highVisc = pumpViscosity[pumpIndex] >= 100.0f;
  float minMargin = highVisc ? BULK_MIN_TRIM_MARGIN_HIGH_G : BULK_MIN_TRIM_MARGIN_LOW_G;
  float maxMargin = highVisc ? 20.0f : 1.25f;

  float trimMargin = targetG * 0.02f * factor;
  if (trimMargin < minMargin) trimMargin = minMargin;
  if (trimMargin > maxMargin) trimMargin = maxMargin;

  float bulkFraction = 1.0f - (trimMargin / targetG);
  if (bulkFraction < 0.10f) bulkFraction = 0.10f;
  return bulkFraction;
}

int activeMicrosteps = 64;
long bulkEndSteps = 0;
long trimStepsRemaining = 0;
unsigned long mixerTimer = 0;
unsigned long mixerSettleTimer = 0;
unsigned long mixerSettleStartCheckTime = 0;
float mixerSettleReferenceWeight = 0.0f;
unsigned long mixBetweenDurationMs = MIX_BETWEEN_DURATION_MS;
unsigned long mixFinalDurationMs = MIX_FINAL_DURATION_MS;
unsigned long mixerSettleGuardMs = MIXER_SETTLE_GUARD_MS;
unsigned long mixerSettleWindowMs = MIXER_SETTLE_WINDOW_MS;
float mixerSettleToleranceG = MIXER_SETTLE_TOLERANCE_G;

// When false, recipes skip the intermediate/final mixing phases entirely
// (e.g. mixing module removed or disabled from the dashboard maintenance tab).
// Persistent setting: deliberately NOT reset by resetRunState().
bool mixingEnabled = true;
unsigned long bulkStartTime = 0;
unsigned long dispenseSequenceStartTime = 0;

AccelStepper& getPump(int pumpIndex) {
  if (pumpIndex < 0 || pumpIndex >= MAX_PUMP_COUNT) {
    return *pumps[0];
  }
  return *pumps[pumpIndex];
}

// Per-pump orientation mapping (mounting flip compensation).
// Contract: +speed means "forward" (dispense/fill) in plumbing terms.
// Pumps mounted on the right side are mechanically mirrored, so their sign is flipped.
static int pumpDirSign(int pumpIndex) {
  if (pumpIndex < 0 || pumpIndex >= MAX_PUMP_COUNT) return 1;
  // 1,3,5,7 (0,2,4,6 idx) => standard ; 2,4,6,8 (1,3,5,7 idx) => mirrored mount
  return (pumpIndex % 2 == 0) ? 1 : -1;
}

// Mirrored pumps (2/4/6/8) invert the shared DIR pin in AccelStepper so all pumps
// use positive speed for forward dispense and negative for retract/suck-back.
static bool pumpMirrored(int pumpIndex) {
  return pumpDirSign(pumpIndex) < 0;
}

static long pumpStepsMoved(AccelStepper& pump) {
  return labs(pump.currentPosition());
}

static void resetFillRateTracking() {
  lastWeightForRate = currentWeight;
  lastWeightRateTime = millis();
  fillRateGps = 0.0f;
}

static void updateFillRate() {
  unsigned long now = millis();
  if (lastWeightRateTime == 0) {
    lastWeightForRate = currentWeight;
    lastWeightRateTime = now;
    return;
  }
  float dt = (now - lastWeightRateTime) / 1000.0f;
  if (dt >= 0.05f) {
    float dW = currentWeight - lastWeightForRate;
    if (dW > 0.0f) {
      fillRateGps = dW / dt;
    } else {
      fillRateGps *= 0.5f;
      if (fillRateGps < 0.001f) fillRateGps = 0.0f;
    }
    lastWeightForRate = currentWeight;
    lastWeightRateTime = now;
  }
}

// Bulk weight stop: overshoot safety near end of planned step run only.
static float getBulkWeightStopMargin(float targetVal) {
  if (targetVal > BULK_LARGE_TARGET_G) {
    return BULK_LARGE_MARGIN_G;
  }
  return getStopLeadG(activePumpIndex, false);
}

static float getBulkSettleUndercut(int pumpIndex, float targetG) {
  if (targetG > BULK_LARGE_TARGET_G) {
    return BULK_LARGE_MARGIN_G;
  }
  return highViscosity[pumpIndex] ? BULK_SETTLE_UNDERCUT_HIGH_G
                                  : BULK_SETTLE_UNDERCUT_LOW_G;
}

static bool shouldStopBulkForWeight(float delivered, float targetVal, long stepsMoved) {
  if (stepsMoved < (long)(bulkEndSteps * BULK_WEIGHT_STOP_MIN_STEP_FRAC)) {
    return false;
  }
  return delivered >= targetVal - getBulkWeightStopMargin(targetVal);
}

static bool bulkRanMeaningfully() {
  if (bulkEndSteps <= 0) return false;
  return bulkStopStepsMoved >= (long)(bulkEndSteps * BULK_LEARN_MIN_STEP_FRAC);
}

static bool shouldStopForWeight(float delivered, float motorStopTarget) {
  if (delivered >= motorStopTarget) return true;
  if (fillRateGps > 0.001f) {
    float predictedOvershoot = fillRateGps * SCALE_LAG_SEC;
    if (delivered + predictedOvershoot >= motorStopTarget) return true;
  }
  return false;
}

// Log bulk yield for diagnostics only. Do NOT blend into trimGain — bulk @ 1/8
// does not predict trim pulse yield at 1/16–1/64.
static void logBulkDeliveryRatio(int pumpIndex, float settledDelivered) {
  if (!bulkRanMeaningfully()) return;
  float expectedBulkG = (float)bulkStopStepsMoved /
    (getStepsPerGram(pumpIndex) * 8.0f);
  if (expectedBulkG > 0.5f && settledDelivered > 0.5f) {
    float bulkRatio = settledDelivered / expectedBulkG;
    if (bulkRatio > 0.4f && bulkRatio < 2.5f) {
      Serial.print("-> Bulk delivery ratio: ");
      Serial.println(bulkRatio, 3);
    }
  }
}

static bool scaleInTolerance(float delivered, float targetVal) {
  return delivered >= targetVal - DISPENSE_TOLERANCE_G &&
         delivered <= targetVal + DISPENSE_TOLERANCE_G;
}

static void finishTrimDispense(const char* reason) {
  Serial.println(reason);
  trimBacklogG = 0.0f;
  trimBacklogWaitStart = 0;
  trimBacklogWaitCycles = 0;
  trimNearLandGateActive = false;
  trimNearLandWaitCycles = 0;
  trimInchwormActive = false;
  dispenseState = DISPENSE_SUCK_BACK;
}

bool isValidPumpIndex(int pumpIndex) {
  return pumpIndex >= 0 && pumpIndex < PUMP_COUNT;
}

float getStepsPerGram(int pumpIndex) {
  return highViscosity[pumpIndex] ? stepsPerGramHigh[pumpIndex] : stepsPerGramLow[pumpIndex];
}

long getRetractSteps(int pumpIndex) {
  // Use the proven fixed retraction amounts. The previous viscosity-factor
  // formula (80 * factor) produced ~80 microsteps (~1.25 full steps at 1/64),
  // which was far too small to actually suck back liquid and prevent drips.
  return highViscosity[pumpIndex] ? retractStepsGlycerol : retractStepsWater;
}

float getRetractSpeed(int pumpIndex) {
  // Magnitude only; caller applies direction relative to each pump's forward.
  return (pumpViscosity[pumpIndex] >= 100.0f) ? 16000.0f : 1200.0f;
}

float getStopLeadG(int pumpIndex, bool isTrimPhase) {
  float visc = (pumpIndex >= 0 && pumpIndex < MAX_PUMP_COUNT)
    ? pumpViscosity[pumpIndex] : PRIME_REF_LOW_VISC_CPS;
  if (visc < 1.0f) visc = 1.0f;
  float factor = log10(visc + 9.0f);
  float factorLow = log10(PRIME_REF_LOW_VISC_CPS + 9.0f);
  float factorHigh = log10(PRIME_REF_HIGH_VISC_CPS + 9.0f);
  float span = factorHigh - factorLow;
  if (span <= 0.0f) span = 1.0f;
  float lowLead = isTrimPhase ? LOW_VISC_TRIM_STOP_LEAD_G : LOW_VISC_STOP_LEAD_G;
  float highLead = isTrimPhase ? HIGH_VISC_TRIM_STOP_LEAD_G : HIGH_VISC_STOP_LEAD_G;
  float lead = lowLead + (highLead - lowLead) * (factor - factorLow) / span;
  if (lead < lowLead) lead = lowLead;
  if (lead > highLead) lead = highLead;
  return lead;
}

static void resetDispenseOrder() {
  for (int index = 0; index < MAX_PUMP_COUNT; ++index) {
    dispenseOrder[index] = index;
  }
}

static int findFirstActivePumpInOrder() {
  for (int orderIndex = 0; orderIndex < PUMP_COUNT; ++orderIndex) {
    int pumpIndex = dispenseOrder[orderIndex];
    if (isValidPumpIndex(pumpIndex) && targetWeights[pumpIndex] > 0.0f) {
      return pumpIndex;
    }
  }
  return -1;
}

static int findNextActivePumpAfter(int currentPumpIndex) {
  int startOrderIndex = -1;
  for (int orderIndex = 0; orderIndex < PUMP_COUNT; ++orderIndex) {
    if (dispenseOrder[orderIndex] == currentPumpIndex) {
      startOrderIndex = orderIndex + 1;
      break;
    }
  }
  if (startOrderIndex < 0) startOrderIndex = 0;

  for (int orderIndex = startOrderIndex; orderIndex < PUMP_COUNT; ++orderIndex) {
    int pumpIndex = dispenseOrder[orderIndex];
    if (isValidPumpIndex(pumpIndex) && targetWeights[pumpIndex] > 0.0f) {
      return pumpIndex;
    }
  }
  return -1;
}

static bool applyDispenseOrderCommand(char* args) {
  int newOrder[MAX_PUMP_COUNT];
  bool seen[MAX_PUMP_COUNT] = {false};
  int count = 0;

  char* token = strtok(args, " ,");
  while (token != nullptr && count < PUMP_COUNT) {
    int pumpNum = atoi(token);
    int pumpIndex = pumpNum - 1;
    if (!isValidPumpIndex(pumpIndex) || seen[pumpIndex]) {
      return false;
    }
    newOrder[count++] = pumpIndex;
    seen[pumpIndex] = true;
    token = strtok(nullptr, " ,");
  }

  if (count != PUMP_COUNT || token != nullptr) {
    return false;
  }

  for (int index = 0; index < PUMP_COUNT; ++index) {
    dispenseOrder[index] = newOrder[index];
  }
  return true;
}

static uint32_t allPumpTargetsMask() {
  return (PUMP_COUNT >= 32) ? 0xFFFFFFFFUL : ((1UL << PUMP_COUNT) - 1UL);
}

static void printTargetAccepted(int pumpIndex, float target) {
  Serial.print("Pump ");
  Serial.print(pumpIndex + 1);
  if (target <= 0.0f) {
    Serial.println(" Target set to: 0.00g (Skip)");
  } else {
    Serial.print(" Target set to: ");
    Serial.print(target, 2);
    Serial.println("g");
  }
}

static bool setTargetForPump(int pumpIndex, float target, bool isZero) {
  if (!isValidPumpIndex(pumpIndex)) {
    Serial.print("ERROR: Target pump must be 1 through ");
    Serial.print(PUMP_COUNT);
    Serial.println(".");
    return false;
  }
  if (!(isZero || target > 1.5f)) {
    Serial.println("ERROR: Invalid target. Must be > 1.50g (or 0.00g to skip).");
    return false;
  }

  targetWeights[pumpIndex] = isZero ? 0.0f : target;
  targetReceivedMask |= (1UL << pumpIndex);
  printTargetAccepted(pumpIndex, targetWeights[pumpIndex]);
  return true;
}

static float dropCalMean(const float* values, int n) {
  if (n <= 0) return 0.0f;
  float sum = 0.0f;
  for (int i = 0; i < n; ++i) sum += values[i];
  return sum / (float)n;
}

static float dropCalMeanLong(const long* values, int n) {
  if (n <= 0) return 0.0f;
  float sum = 0.0f;
  for (int i = 0; i < n; ++i) sum += (float)values[i];
  return sum / (float)n;
}

static float dropCalStd(const float* values, int n, float mean) {
  if (n < 2) return 0.0f;
  float accum = 0.0f;
  for (int i = 0; i < n; ++i) {
    float d = values[i] - mean;
    accum += d * d;
  }
  return sqrtf(accum / (float)(n - 1));
}

static float dropCalStdLong(const long* values, int n, float mean) {
  if (n < 2) return 0.0f;
  float accum = 0.0f;
  for (int i = 0; i < n; ++i) {
    float d = (float)values[i] - mean;
    accum += d * d;
  }
  return sqrtf(accum / (float)(n - 1));
}

static void finishDropCal(const char* reason) {
  AccelStepper& pump = getPump(dropCalPumpIndex);
  pump.stop();
  digitalWrite(SHARED_EN, HIGH);

  float meanMass = dropCalMean(dropCalMasses, dropCalRecorded);
  float stdMass = dropCalStd(dropCalMasses, dropCalRecorded, meanMass);
  float meanSteps = dropCalMeanLong(dropCalStepsArr, dropCalRecorded);
  float stdSteps = dropCalStdLong(dropCalStepsArr, dropCalRecorded, meanSteps);

  Serial.print("DROP_SUMMARY:pump=");
  Serial.print(dropCalPumpIndex + 1);
  Serial.print(",n=");
  Serial.print(dropCalRecorded);
  Serial.print(",mean_mass_g=");
  Serial.print(meanMass, 4);
  Serial.print(",std_mass_g=");
  Serial.print(stdMass, 4);
  Serial.print(",mean_steps=");
  Serial.print(meanSteps, 1);
  Serial.print(",std_steps=");
  Serial.print(stdSteps, 1);
  Serial.print(",total_steps=");
  Serial.print(dropCalTotalSteps);
  Serial.print(",reason=");
  Serial.println(reason);

  sequenceState = SEQ_PROMPT_TARGET;
  promptedTarget = false;
}

static void startDropCalBurst() {
  AccelStepper& pump = getPump(dropCalPumpIndex);
  pump.setCurrentPosition(0);
  pump.moveTo(DROP_CAL_BURST_STEPS);
  pump.setSpeed(DROP_CAL_SPEED / getViscosityFactor(dropCalPumpIndex));
  dropCalInBurst = true;
}

static void startDropCal(int pumpIndex, int dropCount) {
  if (dropCount < 3) dropCount = 3;
  if (dropCount > DROP_CAL_MAX_EVENTS) dropCount = DROP_CAL_MAX_EVENTS;

  dropCalPumpIndex = pumpIndex;
  dropCalTargetCount = dropCount;
  dropCalRecorded = 0;
  dropCalStepsSinceLast = 0;
  dropCalTotalSteps = 0;
  dropCalLastWeight = currentWeight;
  dropCalPrevSample = currentWeight;
  dropCalSettleStart = 0;
  dropCalInBurst = false;

  bool isHighVisc = highViscosity[pumpIndex];
  activeMicrosteps = isHighVisc ? 16 : 64;
  setMicrostepping(activeMicrosteps);
  digitalWrite(SHARED_EN, LOW);
  delayMicroseconds(5);

  Serial.print("DROP_CAL_START:pump=");
  Serial.print(pumpIndex + 1);
  Serial.print(",target_drops=");
  Serial.print(dropCount);
  Serial.print(",burst_steps=");
  Serial.print(DROP_CAL_BURST_STEPS);
  Serial.print(",microsteps=1/");
  Serial.print(activeMicrosteps);
  Serial.print(",detect_g=");
  Serial.println(DROP_CAL_DETECT_G, 3);

  sequenceState = SEQ_DROP_CAL;
  startDropCalBurst();
}

void resetRunState() {
  for (int index = 0; index < PUMP_COUNT; ++index) {
    targetWeights[index] = 0.0f;
  }

  targetEntryPumpIndex = 0;
  targetReceivedMask = 0;
  activePumpIndex = -1;
  calibratingPumpIndex = 0;
  nextPumpIndex = -1;
  promptedTarget = false;
  completionAnnounced = false;
  sequenceState = SEQ_PROMPT_TARGET;
  dispenseState = DISPENSE_IDLE;
  bulkEndSteps = 0;
  trimStepsRemaining = 0;
  newScaleData = false;
  lastScaleUpdateTime = 0;
  settleTimer = 0;
  stopTime = 0;
  lastSettleWeight = 0.0f;
  bulkStartTime = 0;
  dispenseSequenceStartTime = 0;
  mixerTimer = 0;
  mixerSettleTimer = 0;
  mixerSettleStartCheckTime = 0;
  mixerSettleReferenceWeight = 0.0f;
  lastWeightForRate = 0.0f;
  lastWeightRateTime = 0;
  fillRateGps = 0.0f;
  trimPulseCommandedG = 0.0f;
  trimPulseStartDelivered = 0.0f;
  trimPulseRanFull = false;
  trimBacklogG = 0.0f;
  trimBacklogAtPulseStart = 0.0f;
  trimBacklogWaitStart = 0;
  trimBacklogBaselineDelivered = 0.0f;
  trimBacklogWaitCycles = 0;
  trimPulseCount = 0;
  trimCatchupPulseCount = 0;
  trimCoarsePulseCount = 0;
  trimLastPulseCountedCoarse = false;
  trimNearLandGateActive = false;
  trimNearLandBaseline = 0.0f;
  trimNearLandWaitStart = 0;
  trimNearLandWaitCycles = 0;
  trimInchwormActive = false;
  trimInchwormBurstCount = 0;
  trimForceMassPulse = false;
  bulkStopStepsMoved = 0;
  // trimGain* deliberately NOT reset: learned per-pump / per-viscosity.
  // per-viscosity-profile behavior stays valid across runs until reboot.
  
  for (int i = 0; i < MAX_PUMP_COUNT; ++i) {
    maintenancePumpActive[i] = false;
    maintenanceSpeed[i] = 0.0f;
    maintenancePumpEndTime[i] = 0;
  }
}

// Set shared TMC2209 microstepping pins. Valid values: 8, 16, 64.
void setMicrostepping(int resolution) {
  if (resolution == 8) {
    digitalWrite(SHARED_MS1, LOW);
    digitalWrite(SHARED_MS2, LOW);
  } else if (resolution == 16) {
    digitalWrite(SHARED_MS1, HIGH);
    digitalWrite(SHARED_MS2, LOW);
  } else if (resolution == 64) {
    digitalWrite(SHARED_MS1, LOW);
    digitalWrite(SHARED_MS2, HIGH);
  } else {
    Serial.println("WARNING: Invalid microstepping value, defaulting to 16 (8/16/64 supported)");
    digitalWrite(SHARED_MS1, HIGH);
    digitalWrite(SHARED_MS2, LOW);
  }
}

void printPumpLabel(int pumpIndex) {
  Serial.print("Pump ");
  Serial.print(pumpIndex + 1);
}

void printTargetPrompt(int pumpIndex) {
  printPumpLabel(pumpIndex);
  Serial.print(" Viscosity: ");
  Serial.println(highViscosity[pumpIndex] ? "HIGH (Glycerol)" : "LOW (Water)");
  printPumpLabel(pumpIndex);
  Serial.println(" Enter weight (g):");
}

bool isScaleSettled(int pumpIndex) {
  float factor = getViscosityFactor(pumpIndex);
  unsigned long settleGuardTime;
  unsigned long settleWindowMs;
  float settleToleranceG;
  bool nearBand = trimNearLandGateActive || trimInchwormActive ||
                  (lastTrimPulseSizeG > 0.0f && lastTrimPulseSizeG <= TRIM_NEAR_BAND_G);

  if (dispenseState == DISPENSE_SETTLE_TRIM) {
    if (highViscosity[pumpIndex]) {
      settleGuardTime = HIGH_VISC_SETTLE_GUARD_MS;
      settleWindowMs = SETTLE_TIMEOUT_MS;
      settleToleranceG = TRIM_SETTLE_TOLERANCE_G;
    } else if (nearBand) {
      settleGuardTime = TRIM_GUARD_NEAR_MS;
      settleWindowMs = TRIM_SETTLE_NEAR_MS;
      settleToleranceG = TRIM_SETTLE_TOLERANCE_G;
    } else if (lastTrimPulseSizeG > 0.15f) {
      settleGuardTime = TRIM_GUARD_LARGE_MS;
      settleWindowMs = TRIM_SETTLE_TIMEOUT_MS;
      settleToleranceG = TRIM_SETTLE_TOLERANCE_G;
    } else if (lastTrimPulseSizeG > 0.05f) {
      settleGuardTime = TRIM_GUARD_MED_MS;
      settleWindowMs = TRIM_SETTLE_TIMEOUT_MS;
      settleToleranceG = TRIM_SETTLE_TOLERANCE_G;
    } else {
      settleGuardTime = TRIM_GUARD_FINE_MS;
      settleWindowMs = TRIM_SETTLE_TIMEOUT_MS;
      settleToleranceG = TRIM_SETTLE_TOLERANCE_G;
    }
  } else {
    settleGuardTime = (unsigned long)(BULK_SETTLE_GUARD_MS * factor);
    settleWindowMs = SETTLE_TIMEOUT_MS;
    settleToleranceG = BULK_SETTLE_TOLERANCE_G;
  }

  if (millis() - stopTime < settleGuardTime) {
    return false;
  }

  bool hadNewData = newScaleData;
  newScaleData = false;

  if (!hadNewData) {
    return false;
  }

  unsigned long scaledSettleTimeout = (unsigned long)(settleWindowMs * factor);
  if (millis() - settleTimer >= scaledSettleTimeout) {
    if (abs(currentWeight - lastSettleWeight) < settleToleranceG) {
      return true;
    }
    lastSettleWeight = currentWeight;
    settleTimer = millis();
  }
  return false;
}

static void captureBulkStop(float delivered, long stepsMoved) {
  bulkStopDelivered = delivered;
  bulkStopStepsMoved = stepsMoved;
  // Motor stopped: lag prediction must not reuse stale fill rate.
  fillRateGps = 0.0f;
}

void setupBulkFillHelper(int pumpIndex, float targetG) {
  AccelStepper& pump = getPump(pumpIndex);

  startWeight = currentWeight;
  lastScaleUpdateTime = millis();

  float adjustedTarget = targetG - getStopLeadG(pumpIndex, false);
  float bulkFraction = getDynamicBulkFraction(pumpIndex, targetG);
  float bulkTargetG = adjustedTarget * bulkFraction;
  float stepsPerGram = getStepsPerGram(pumpIndex);

  // Leave room under target: fraction keeps trim budget, settle undercut
  // accounts for post-stop in-flight. Large targets (>30g) use a 5g margin.
  float settleUndercut = getBulkSettleUndercut(pumpIndex, targetG);
  float effectiveBulkG = bulkTargetG - settleUndercut;
  float minEffective = targetG * BULK_MIN_EFFECTIVE_FRAC;
  if (effectiveBulkG < minEffective) {
    effectiveBulkG = minEffective;
  }

  activeMicrosteps = 8;
  setMicrostepping(activeMicrosteps);

  bulkEndSteps = (long)(effectiveBulkG * stepsPerGram * activeMicrosteps);
  if (bulkEndSteps < 100) bulkEndSteps = 100;
  pump.setCurrentPosition(0);
  pump.setSpeed(BULK_SPEED / getViscosityFactor(pumpIndex));

  digitalWrite(SHARED_EN, LOW);
  bulkStartTime = millis();
  resetFillRateTracking();
  trimPulseCommandedG = 0.0f;
  trimPulseRanFull = false;
  trimBacklogG = 0.0f;
  trimBacklogAtPulseStart = 0.0f;
  trimBacklogWaitStart = 0;
  trimBacklogWaitCycles = 0;
  trimPulseCount = 0;
  trimCatchupPulseCount = 0;
  trimCoarsePulseCount = 0;
  trimCatchupPhase = false;
  trimLastPulseCountedCoarse = false;
  trimShrinkNextPulse = false;
  trimNearLandGateActive = false;
  trimNearLandBaseline = 0.0f;
  trimNearLandWaitStart = 0;
  trimNearLandWaitCycles = 0;
  trimInchwormActive = false;
  trimInchwormBurstCount = 0;
  trimForceMassPulse = false;
  bulkStopStepsMoved = 0;
  dispenseState = DISPENSE_BULK_FILL;

  Serial.print("-> Aiming for ");
  Serial.print(effectiveBulkG, 2);
  Serial.print("g bulk steps (");
  Serial.print(bulkEndSteps);
  Serial.print(" @ 1/8, settle undercut ");
  Serial.print(settleUndercut, 2);
  Serial.print("g");
  if (targetG > BULK_LARGE_TARGET_G) {
    Serial.print(", large-dose margin");
  }
  Serial.println(")");
}

void prepareTrimPulseHelper(int pumpIndex, float remaining, bool catchupPhase) {
  if (remaining <= 0.0f) return;

  AccelStepper& pump = getPump(pumpIndex);
  bool isHighVisc = highViscosity[pumpIndex];
  float stepsPerGram = getStepsPerGram(pumpIndex);
  float factor = getViscosityFactor(pumpIndex);
  float pulseG;
  // Never claim mass that would land past the upper tolerance band.
  float maxSafeVsTol = remaining - DISPENSE_TOLERANCE_G;
  if (maxSafeVsTol < 0.003f) {
    return;
  }

  // Last ~2 drops: inchworm 64 µsteps @ 1/64 (water). Mass caps alone still
  // stack phantom catch-ups; DROP_CAL-style bursts wait for a real land.
  bool useInchworm = !isHighVisc && remaining <= TRIM_INCHWORM_REMAIN_G && !trimForceMassPulse;
  trimForceMassPulse = false;
  if (useInchworm) {
    if (trimInchwormBurstCount >= TRIM_INCHWORM_MAX_BURSTS) {
      return;
    }
    pulseG = min(TRIM_HALF_DROP_G, maxSafeVsTol);
    if (pulseG < 0.003f) {
      return;
    }

    dispenseState = DISPENSE_TRIM_PULSE;
    trimCatchupPhase = catchupPhase;
    trimInchwormActive = true;
    trimInchwormBurstCount++;
    trimLastPulseCountedCoarse = false;
    if (catchupPhase) {
      trimCatchupPulseCount++;
    } else {
      trimPulseCount++;
    }
    activeMicrosteps = 64;
    setMicrostepping(activeMicrosteps);
    trimStepsRemaining = DROP_CAL_BURST_STEPS;
    if (trimStepsRemaining < 2) trimStepsRemaining = 2;
    trimPulseCommandedG = pulseG;
    lastTrimPulseSizeG = pulseG;
    trimPulseStartDelivered = currentWeight - startWeight;
    trimBacklogAtPulseStart = trimBacklogG;
    trimBacklogWaitStart = 0;
    trimBacklogWaitCycles = 0;
    trimNearLandGateActive = false;
    // Session baseline: accumulate tip fill across bursts until a drop lands
    // (same idea as DROP_CAL), instead of a multi-second hang wait per 64 µsteps.
    if (trimInchwormBurstCount == 1) {
      trimNearLandBaseline = trimPulseStartDelivered;
      trimNearLandWaitCycles = 0;
    }
    pump.setCurrentPosition(0);
    pump.setSpeed(DROP_CAL_SPEED / factor);
    resetFillRateTracking();

    Serial.print("-> Inchworm burst #");
    Serial.print(trimInchwormBurstCount);
    Serial.print(": ");
    Serial.print(DROP_CAL_BURST_STEPS);
    Serial.println(" uSteps @ 1/64");
    return;
  }

  trimInchwormActive = false;

  if (catchupPhase) {
    // Catch-up capped at half a measured drop so stacked tip mass cannot land over target.
    pulseG = min(remaining * 0.40f, TRIM_PULSE_CATCHUP_MAX_G);
    float maxSafe = remaining - TRIM_CATCHUP_HEADROOM_G;
    if (maxSafe < 0.003f) {
      return;
    }
    pulseG = min(pulseG, maxSafe);
    pulseG = min(pulseG, TRIM_HALF_DROP_G);
    pulseG = min(pulseG, maxSafeVsTol);
    if (pulseG < 0.003f) {
      return;
    }
  } else {
    float coefficient;
    if (remaining > 0.50f) {
      coefficient = 0.58f;
    } else if (remaining > 0.20f) {
      coefficient = 0.48f;
    } else if (remaining > 0.08f) {
      coefficient = 0.40f;
    } else {
      coefficient = 0.35f;
    }

    float absMaxPulse;
    if (remaining > TRIM_COARSE_REMAIN_G) {
      absMaxPulse = TRIM_PULSE_MAX_COARSE_G;
    } else if (remaining > 0.80f) {
      absMaxPulse = TRIM_PULSE_MAX_LARGE_G;
    } else if (remaining > 0.25f) {
      absMaxPulse = TRIM_PULSE_MAX_MED_G;
    } else if (remaining > DROP_MASS_DEFAULT_G * 2.0f) {
      absMaxPulse = TRIM_PULSE_MAX_SMALL_G;
    } else if (remaining > DROP_MASS_DEFAULT_G) {
      absMaxPulse = TRIM_PULSE_NEAR_G;       // ≤1 full drop
    } else {
      absMaxPulse = TRIM_PULSE_MAX_FINE_G;   // ≤~½ drop near target
    }

    if (trimShrinkNextPulse) {
      trimShrinkNextPulse = false;
      coefficient *= 0.40f;
      absMaxPulse = min(absMaxPulse, TRIM_HALF_DROP_G);
    }

    // Soft near-band floors: avoid forcing 6–12mg pulses that exceed safe remainder.
    float minPulseG;
    if (remaining > 0.08f) {
      minPulseG = 0.005f * factor;
    } else if (remaining > DROP_MASS_DEFAULT_G) {
      minPulseG = 0.004f;
    } else {
      minPulseG = 0.003f;
    }
    pulseG = remaining * coefficient;
    if (remaining >= minPulseG * 2.0f) {
      pulseG = max(minPulseG, pulseG);
    }
    pulseG = min(pulseG, absMaxPulse);
    if (remaining <= DROP_MASS_DEFAULT_G) {
      pulseG = min(pulseG, TRIM_HALF_DROP_G);
    }
    float maxPulseG = remaining - TRIM_CATCHUP_HEADROOM_G;
    if (maxPulseG < 0.002f) maxPulseG = 0.002f;
    pulseG = min(pulseG, maxPulseG);
    pulseG = min(pulseG, maxSafeVsTol);
  }

  if (pulseG <= 0.0f) {
    return;
  }

  dispenseState = DISPENSE_TRIM_PULSE;
  trimCatchupPhase = catchupPhase;
  if (catchupPhase) {
    trimCatchupPulseCount++;
    trimLastPulseCountedCoarse = false;
  } else if (remaining > TRIM_FINE_ENTRY_G) {
    trimCoarsePulseCount++;
    trimLastPulseCountedCoarse = true;
  } else {
    trimPulseCount++;
    trimLastPulseCountedCoarse = false;
  }
  activeMicrosteps = isHighVisc ? 16 : 64;
  setMicrostepping(activeMicrosteps);

  float gain = getTrimGain(pumpIndex);
  if (gain < 0.5f) gain = 0.5f;
  if (gain > 1.25f) gain = 1.25f;
  trimStepsRemaining = (long)((pulseG * stepsPerGram * activeMicrosteps) / gain);
  if (trimStepsRemaining < 2) trimStepsRemaining = 2;
  trimPulseCommandedG = pulseG;
  lastTrimPulseSizeG = pulseG;
  trimPulseStartDelivered = currentWeight - startWeight;
  trimBacklogAtPulseStart = trimBacklogG;
  trimBacklogWaitStart = 0;
  trimBacklogWaitCycles = 0;
  trimNearLandGateActive = false;
  pump.setCurrentPosition(0);

  float trickleSpeed = TRICKLE_SPEED / factor;
  if (remaining < 0.10f) {
    trickleSpeed = TRIM_SPEED_FINAL;
  } else if (remaining < 0.30f) {
    trickleSpeed = TRICKLE_SPEED / (factor * 2.0f);
  }
  pump.setSpeed(trickleSpeed);
  resetFillRateTracking();

  Serial.print(catchupPhase ? "-> Catch-up Pulse: +" : "-> Micro-Pulse: +");
  Serial.print(pulseG, 3);
  Serial.println("g");
}

static bool withinOneDropOfTarget(float delivered, float targetVal) {
  float err = delivered - targetVal;
  return err >= -DROP_MASS_DEFAULT_G && err <= DROP_MASS_DEFAULT_G;
}

static void armNearLandGate(float delivered) {
  trimNearLandGateActive = true;
  trimNearLandBaseline = delivered;
  trimNearLandWaitStart = millis();
  trimNearLandWaitCycles = 0;
  Serial.print("-> Near-target land gate (baseline ");
  Serial.print(delivered, 3);
  Serial.println("g); waiting for drop to land...");
}

// Returns true if caller should return (still waiting). False = gate resolved.
static bool nearLandGateBlocksNextPulse(float delivered, float targetVal, float remaining) {
  if (!trimNearLandGateActive) {
    return false;
  }

  unsigned long now = millis();
  dispenseSequenceStartTime = now;
  stopTime = now;  // Refresh settle watchdog during hang waits
  float delta = delivered - trimNearLandBaseline;

  if (delta >= DROP_CAL_DETECT_G) {
    Serial.print("-> Drop landed (+");
    Serial.print(delta, 3);
    Serial.println("g).");
    trimNearLandGateActive = false;
    trimBacklogG = 0.0f;
    return false;
  }

  if (withinOneDropOfTarget(delivered, targetVal) ||
      scaleInTolerance(delivered, targetVal)) {
    trimNearLandGateActive = false;
    return false;
  }

  if (now - trimNearLandWaitStart < TRIM_BACKLOG_WAIT_NEAR_MS) {
    return true;
  }

  trimNearLandWaitCycles++;
  if (trimNearLandWaitCycles < MAX_BACKLOG_WAIT_CYCLES) {
    trimNearLandWaitStart = now;
    Serial.print("-> Hang wait cycle ");
    Serial.print(trimNearLandWaitCycles);
    Serial.println("; still no land.");
    return true;
  }

  Serial.println("-> Hang timeout; allowing next decision.");
  trimNearLandGateActive = false;
  trimBacklogG = 0.0f;
  trimShrinkNextPulse = true;
  return false;
}

static void requestTrimPulse(int pumpIndex, float remaining, float delivered, float targetVal) {
  if (remaining <= TRIM_CATCHUP_HEADROOM_G && remaining > DROP_MASS_DEFAULT_G) {
    return;
  }
  if (delivered >= targetVal - DISPENSE_TOLERANCE_G) return;
  if (withinOneDropOfTarget(delivered, targetVal) && remaining <= TRIM_HALF_DROP_G) {
    return;
  }
  if (delivered + trimBacklogG >= targetVal - DISPENSE_TOLERANCE_G * 0.5f &&
      remaining > DISPENSE_TOLERANCE_G * 2.0f) {
    return;
  }
  // Prefer inchworm in the last ~2 drops even when main pulse budget is spent.
  if (remaining <= TRIM_INCHWORM_REMAIN_G && !highViscosity[pumpIndex] &&
      trimInchwormBurstCount < TRIM_INCHWORM_MAX_BURSTS) {
    prepareTrimPulseHelper(pumpIndex, remaining, trimPulseCount >= MAX_TRIM_PULSES_PER_PUMP);
    return;
  }
  // Large residual (post bulk-margin): coarse pulses do not consume fine budget.
  if (remaining > TRIM_FINE_ENTRY_G) {
    if (trimCoarsePulseCount < MAX_TRIM_COARSE_PULSES) {
      prepareTrimPulseHelper(pumpIndex, remaining, false);
    }
    return;
  }
  if (trimPulseCount >= MAX_TRIM_PULSES_PER_PUMP) {
    if (trimCatchupPulseCount < MAX_TRIM_CATCHUP_PULSES &&
        delivered < targetVal - DISPENSE_TOLERANCE_G &&
        remaining > TRIM_CATCHUP_HEADROOM_G) {
      prepareTrimPulseHelper(pumpIndex, remaining, true);
    }
    return;
  }
  prepareTrimPulseHelper(pumpIndex, remaining, false);
}

static unsigned long trimBacklogWaitDuration(float delivered, float targetVal, float backlogG) {
  if (delivered + backlogG >= targetVal - TRIM_NEAR_BAND_G) {
    return TRIM_BACKLOG_WAIT_NEAR_MS;
  }
  return TRIM_BACKLOG_WAIT_MS;
}

void startPumpDispense(int pumpIndex) {
  float targetG = targetWeights[pumpIndex];
  float adjustedTarget = targetG - getStopLeadG(pumpIndex, false);

  // Restart the dispense watchdog for each pump. Without this, time spent
  // mixing between pumps (3+ min) counts against MAX_DISPENSE_TIMEOUT_MS and
  // every pump after the first would trip the timeout immediately.
  dispenseSequenceStartTime = millis();

  setupBulkFillHelper(pumpIndex, targetG);

  Serial.print("Starting Pump ");
  Serial.print(pumpIndex + 1);
  Serial.println(" Dispense.");
  Serial.print("-> Target: ");
  Serial.print(targetG, 2);
  Serial.print("g (Motor stop target: ");
  Serial.print(adjustedTarget, 2);
  Serial.println("g)");
}

void startFirstActivePump() {
  int firstActivePump = findFirstActivePumpInOrder();
  if (firstActivePump < 0) {
    Serial.println("All targets set to 0.00g. Dispensing skipped.");
    resetRunState();
    return;
  }

  Serial.println("Starting Sequential Dispense.");
  Serial.print("Dispense order: ");
  for (int orderIndex = 0; orderIndex < PUMP_COUNT; ++orderIndex) {
    Serial.print(dispenseOrder[orderIndex] + 1);
    if (orderIndex < PUMP_COUNT - 1) Serial.print(",");
  }
  Serial.println();
  activePumpIndex = firstActivePump;
  sequenceState = SEQ_DISPENSE_ACTIVE;
  startPumpDispense(activePumpIndex);
}

void completeRun() {
  Serial.println("\nEntire multi-pump dispensing sequence completed successfully!");
  Serial.println("========================================\n");
  resetRunState();
}

bool dispensePump(int pumpIndex, float targetVal, long retractSteps, float retractSpeed) {
  if (errorLocked) return true;
  AccelStepper& pump = getPump(pumpIndex);
  bool isHighVisc = highViscosity[pumpIndex];

  float delivered = currentWeight - startWeight;
  float remaining = targetVal - delivered;
  bool inTrimPhase = (dispenseState == DISPENSE_TRIM_PULSE);
  float motorStopTarget = targetVal - getStopLeadG(pumpIndex, inTrimPhase);

  switch (dispenseState) {
    case DISPENSE_IDLE:
      break;

    case DISPENSE_BULK_FILL: {
      long stepsMoved = pumpStepsMoved(pump);
      if (stepsMoved >= bulkEndSteps) {
        pump.stop();
        stopTime = millis();
        captureBulkStop(delivered, stepsMoved);

        activeMicrosteps = isHighVisc ? 16 : 64;
        setMicrostepping(activeMicrosteps);

        dispenseState = DISPENSE_SETTLE_BULK;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
        Serial.print("-> Bulk Fill complete at ");
        Serial.print(stepsMoved);
        Serial.print(" steps (delivered ");
        Serial.print(delivered, 2);
        Serial.println("g). Settling scale...");
      } else if (shouldStopBulkForWeight(delivered, targetVal, stepsMoved)) {
        pump.stop();
        stopTime = millis();
        captureBulkStop(delivered, stepsMoved);

        activeMicrosteps = isHighVisc ? 16 : 64;
        setMicrostepping(activeMicrosteps);

        Serial.print("-> Bulk overshoot safety stop (Delivered: ");
        Serial.print(delivered, 2);
        Serial.print("g, margin ");
        Serial.print(getBulkWeightStopMargin(targetVal), 2);
        Serial.println("g). Halting to Settle...");

        dispenseState = DISPENSE_SETTLE_BULK;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else {
        unsigned long elapsed = millis() - bulkStartTime;
        float scaledBulkSpeed = BULK_SPEED / getViscosityFactor(activePumpIndex);
        float currentTargetSpeed = scaledBulkSpeed;
        const unsigned long RAMP_TIME_MS = 400;
        const float START_SPEED = 1500.0f;

        if (elapsed < RAMP_TIME_MS) {
          currentTargetSpeed = START_SPEED + ((scaledBulkSpeed - START_SPEED) * ((float)elapsed / RAMP_TIME_MS));
        }
        pump.setSpeed(currentTargetSpeed);
        pump.runSpeed();
      }
      break;
    }

    case DISPENSE_SETTLE_BULK:
      if (isScaleSettled(pumpIndex)) {
        Serial.print("-> Bulk Settled at: ");
        Serial.print(delivered, 2);
        Serial.println("g");

        logBulkDeliveryRatio(pumpIndex, delivered);

        if (scaleInTolerance(delivered, targetVal)) {
          Serial.println("-> Bulk settled in tolerance. Finishing.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else if (delivered >= targetVal + DISPENSE_TOLERANCE_G) {
          Serial.print("-> Bulk overshoot by ");
          Serial.print(delivered - targetVal, 2);
          Serial.println("g. Halting dispense.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else {
          prepareTrimPulseHelper(pumpIndex, remaining, false);
        }
      }
      break;

    case DISPENSE_TRIM_PULSE:
      if (shouldStopForWeight(delivered, motorStopTarget)) {
        pump.stop();
        stopTime = millis();
        trimPulseRanFull = false;
        fillRateGps = 0.0f;

        Serial.print("-> Target reached during Trim Pulse (Delivered: ");
        Serial.print(delivered, 2);
        Serial.println("g). Halting to Settle...");

        dispenseState = DISPENSE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else if (pumpStepsMoved(pump) >= trimStepsRemaining) {
        pump.stop();
        stopTime = millis();
        trimPulseRanFull = true;
        fillRateGps = 0.0f;
        dispenseState = DISPENSE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else {
        pump.runSpeed();
      }
      break;

    case DISPENSE_SETTLE_TRIM:
      if (isScaleSettled(pumpIndex)) {
        float actualPulseG = 0.0f;
        bool hadPulseAccount = false;
        if (trimPulseCommandedG > 0.0f) {
          actualPulseG = delivered - trimPulseStartDelivered;
          if (actualPulseG < 0.0f) actualPulseG = 0.0f;
          hadPulseAccount = true;

          if (trimPulseRanFull && trimPulseCommandedG > 0.015f &&
              actualPulseG > 0.008f &&
              trimBacklogAtPulseStart <= DISPENSE_TOLERANCE_G * 0.5f) {
            float ratio = actualPulseG / trimPulseCommandedG;
            if (ratio >= 0.5f && ratio <= 2.0f) {
              blendTrimGain(pumpIndex, ratio, 0.3f);
              Serial.print("-> Pulse gain updated: ");
              Serial.println(getTrimGain(pumpIndex), 3);
            }
          }

          if (!trimCatchupPhase && trimPulseRanFull && trimPulseCommandedG > 0.10f &&
              actualPulseG < trimPulseCommandedG * 0.15f) {
            if (trimLastPulseCountedCoarse && trimCoarsePulseCount > 0) {
              trimCoarsePulseCount--;
            } else if (trimPulseCount > 0) {
              trimPulseCount--;
            }
          }

          float unregistered = trimPulseCommandedG - actualPulseG;
          if (trimPulseRanFull && unregistered > 0.008f &&
              actualPulseG < trimPulseCommandedG * 0.25f &&
              actualPulseG < 0.04f) {
            trimBacklogG += min(unregistered, BACKLOG_ACCUM_MAX_G);
          }
          if (trimBacklogG < 0.0f) trimBacklogG = 0.0f;
          if (trimBacklogG > BACKLOG_ACCUM_MAX_G) {
            trimBacklogG = BACKLOG_ACCUM_MAX_G;
          }
          trimPulseCommandedG = 0.0f;
        }

        if (delivered >= targetVal - DISPENSE_TOLERANCE_G) {
          // At or above lower band: no more pulses. Wait for hanging mass, then finish.
          unsigned long now = millis();
          dispenseSequenceStartTime = now;
          trimNearLandGateActive = false;
          if (trimBacklogWaitStart == 0) {
            trimBacklogWaitStart = now;
            trimBacklogBaselineDelivered = delivered;
            trimBacklogWaitCycles = 0;
            Serial.print("-> At/over lower band; confirming settle (");
            Serial.print(delivered, 2);
            Serial.println("g)...");
          } else if (delivered > trimBacklogBaselineDelivered + 0.003f) {
            trimBacklogBaselineDelivered = delivered;
            trimBacklogWaitStart = now;
            // Keep waiting while mass is still landing.
          } else if (now - trimBacklogWaitStart >= TRIM_FINISH_CONFIRM_MS) {
            trimBacklogWaitCycles++;
            if (trimBacklogWaitCycles < TRIM_FINISH_CONFIRM_CYCLES) {
              trimBacklogWaitStart = now;
              trimBacklogBaselineDelivered = delivered;
            } else if (scaleInTolerance(delivered, targetVal)) {
              finishTrimDispense("-> Scale confirmed in tolerance. Finishing.");
            } else if (delivered >= targetVal + DISPENSE_TOLERANCE_G) {
              finishTrimDispense("-> Over target after settle confirm. Finishing.");
            } else if (withinOneDropOfTarget(delivered, targetVal)) {
              finishTrimDispense("-> Within one drop after settle confirm. Finishing.");
            } else {
              finishTrimDispense("-> Lower band stable. Finishing.");
            }
          }
        } else if (withinOneDropOfTarget(delivered, targetVal) &&
                   remaining <= DROP_MASS_DEFAULT_G) {
          // Already inside one-drop band under target — tip settle then accept.
          unsigned long now = millis();
          dispenseSequenceStartTime = now;
          trimNearLandGateActive = false;
          if (trimBacklogWaitStart == 0) {
            trimBacklogWaitStart = now;
            trimBacklogBaselineDelivered = delivered;
            trimBacklogWaitCycles = 0;
            Serial.print("-> Within one-drop band; confirming (");
            Serial.print(delivered, 2);
            Serial.println("g)...");
          } else if (delivered > trimBacklogBaselineDelivered + DROP_CAL_DETECT_G) {
            trimBacklogBaselineDelivered = delivered;
            trimBacklogWaitStart = now;
            if (delivered >= targetVal - DISPENSE_TOLERANCE_G) {
              // Fall through next loop to lower-band finish.
            }
          } else if (now - trimBacklogWaitStart >= TRIM_FINISH_CONFIRM_MS) {
            finishTrimDispense("-> Accept within one drop of target. Finishing.");
          }
        } else if (remaining <= TRIM_NEAR_BAND_G || trimNearLandGateActive ||
                   trimInchwormActive) {
          // Near-target / inchworm: DROP_CAL-style accumulate tips, short settle between
          // bursts (isScaleSettled already ran) — do not hang-wait 3s after every 64 µsteps.
          dispenseSequenceStartTime = millis();
          stopTime = millis();

          if (trimInchwormActive ||
              (remaining <= TRIM_INCHWORM_REMAIN_G && !highViscosity[pumpIndex])) {
            float sessionDelta = delivered - trimNearLandBaseline;
            if (trimInchwormBurstCount >= 1 && sessionDelta >= DROP_CAL_DETECT_G) {
              Serial.print("-> Inchworm drop landed (+");
              Serial.print(sessionDelta, 3);
              Serial.println("g).");
              trimBacklogG = 0.0f;
              trimNearLandGateActive = false;
              trimNearLandBaseline = delivered;
            }

            if (delivered >= targetVal - DISPENSE_TOLERANCE_G ||
                scaleInTolerance(delivered, targetVal) ||
                withinOneDropOfTarget(delivered, targetVal)) {
              finishTrimDispense("-> Near-target: within band after inchworm. Finishing.");
            } else if (remaining <= TRIM_HALF_DROP_G) {
              finishTrimDispense("-> Under by <=1/2 drop. Accepting.");
            } else if (trimInchwormBurstCount >= TRIM_INCHWORM_MAX_BURSTS) {
              // Tip not releasing — one forced mass half-drop, then accept next settle.
              trimForceMassPulse = true;
              trimInchwormActive = false;
              prepareTrimPulseHelper(pumpIndex, remaining, true);
              if (dispenseState != DISPENSE_TRIM_PULSE) {
                finishTrimDispense("-> Inchworm budget spent. Finishing.");
              }
            } else {
              trimNearLandGateActive = false;
              requestTrimPulse(pumpIndex, remaining, delivered, targetVal);
              if (dispenseState != DISPENSE_TRIM_PULSE) {
                trimForceMassPulse = true;
                prepareTrimPulseHelper(pumpIndex, remaining, true);
                if (dispenseState != DISPENSE_TRIM_PULSE) {
                  finishTrimDispense("-> Near-target: no further pulse; accepting. Finishing.");
                }
              }
            }
          } else {
            if (!trimNearLandGateActive) {
              if (hadPulseAccount && actualPulseG >= DROP_CAL_DETECT_G) {
                Serial.print("-> Drop already on scale (+");
                Serial.print(actualPulseG, 3);
                Serial.println("g); deciding without hang wait.");
              } else if (hadPulseAccount) {
                armNearLandGate(delivered);
              }
            }
            if (nearLandGateBlocksNextPulse(delivered, targetVal, remaining)) {
              break;
            }
            if (delivered >= targetVal - DISPENSE_TOLERANCE_G ||
                scaleInTolerance(delivered, targetVal) ||
                withinOneDropOfTarget(delivered, targetVal)) {
              finishTrimDispense("-> Near-target: scale OK after land gate. Finishing.");
            } else if (remaining <= TRIM_HALF_DROP_G) {
              finishTrimDispense("-> Under by <=1/2 drop with no land. Accepting.");
            } else {
              requestTrimPulse(pumpIndex, remaining, delivered, targetVal);
              if (dispenseState != DISPENSE_TRIM_PULSE) {
                finishTrimDispense("-> Cannot pulse further. Finishing.");
              }
            }
          }
        } else if (remaining > TRIM_FINE_ENTRY_G) {
          // Close large residual with coarse pulses; never half-drop catch-up here.
          if (trimCoarsePulseCount < MAX_TRIM_COARSE_PULSES) {
            prepareTrimPulseHelper(pumpIndex, remaining, false);
            if (dispenseState != DISPENSE_TRIM_PULSE) {
              Serial.println("-> Trim coarse: cannot pulse further.");
              dispenseState = DISPENSE_SUCK_BACK;
            }
          } else if (withinOneDropOfTarget(delivered, targetVal)) {
            finishTrimDispense("-> Coarse trim limit; within one drop. Finishing.");
          } else if (delivered < targetVal - DISPENSE_TOLERANCE_G) {
            Serial.println("-> Trim coarse exhausted; still under target.");
            dispenseState = DISPENSE_SUCK_BACK;
          } else {
            finishTrimDispense("-> Coarse trim limit; best achievable. Finishing.");
          }
        } else if (trimPulseCount >= MAX_TRIM_PULSES_PER_PUMP &&
                   !(remaining <= TRIM_INCHWORM_REMAIN_G && !highViscosity[pumpIndex] &&
                     trimInchwormBurstCount < TRIM_INCHWORM_MAX_BURSTS)) {
          if (delivered < targetVal - DISPENSE_TOLERANCE_G &&
              remaining > TRIM_CATCHUP_HEADROOM_G &&
              trimCatchupPulseCount < MAX_TRIM_CATCHUP_PULSES) {
            prepareTrimPulseHelper(pumpIndex, remaining, true);
          } else if (withinOneDropOfTarget(delivered, targetVal)) {
            finishTrimDispense("-> Trim limit; within one drop. Finishing.");
          } else if (delivered < targetVal - DISPENSE_TOLERANCE_G) {
            Serial.println("-> Trim exhausted; still under target.");
            dispenseState = DISPENSE_SUCK_BACK;
          } else {
            finishTrimDispense("-> Trim pulse limit; best achievable. Finishing.");
          }
        } else if (trimBacklogG > BACKLOG_FINISH_MAX_G) {
          unsigned long now = millis();
          dispenseSequenceStartTime = now;

          if (trimBacklogWaitStart == 0) {
            trimBacklogWaitStart = now;
            trimBacklogBaselineDelivered = delivered;
            Serial.print("-> Waiting for mass to land (backlog ");
            Serial.print(trimBacklogG, 3);
            Serial.print("g, scale ");
            Serial.print(delivered, 2);
            Serial.println("g)...");
          } else if (delivered > trimBacklogBaselineDelivered + DROP_CAL_DETECT_G) {
            float landed = delivered - trimBacklogBaselineDelivered;
            trimBacklogG -= landed;
            if (trimBacklogG < 0.0f) trimBacklogG = 0.0f;
            trimBacklogBaselineDelivered = delivered;
            trimBacklogWaitStart = now;
            trimBacklogWaitCycles = 0;
            if (scaleInTolerance(delivered, targetVal) ||
                withinOneDropOfTarget(delivered, targetVal)) {
              finishTrimDispense("-> Mass landed; within target band. Finishing.");
            }
          } else if (now - trimBacklogWaitStart >=
                     trimBacklogWaitDuration(delivered, targetVal, trimBacklogG)) {
            trimBacklogWaitCycles++;
            if (scaleInTolerance(delivered, targetVal) ||
                withinOneDropOfTarget(delivered, targetVal)) {
              finishTrimDispense("-> Scale settled in one-drop band. Finishing.");
            } else if (trimBacklogWaitCycles >= MAX_BACKLOG_WAIT_CYCLES) {
              if (trimBacklogG > 0.01f) {
                Serial.println("-> Backlog did not land; clearing phantom estimate.");
                trimBacklogG = 0.0f;
                trimShrinkNextPulse = true;
              }
              trimBacklogWaitStart = 0;
              trimBacklogWaitCycles = 0;
              if (remaining <= TRIM_NEAR_BAND_G) {
                armNearLandGate(delivered);
              } else if (delivered < targetVal - DISPENSE_TOLERANCE_G) {
                requestTrimPulse(pumpIndex, remaining, delivered, targetVal);
              } else {
                finishTrimDispense("-> Residual within tolerance band. Finishing.");
              }
            } else {
              trimBacklogWaitStart = now;
            }
          }
        } else {
          requestTrimPulse(pumpIndex, remaining - trimBacklogG, delivered, targetVal);
          if (dispenseState == DISPENSE_TRIM_PULSE && remaining <= TRIM_NEAR_BAND_G) {
            // Armed after next settle.
          }
        }
      }
      break;

    case DISPENSE_SUCK_BACK:
      pump.setCurrentPosition(0);
      if (isHighVisc) {
        pump.setSpeed(-abs(HIGH_VISC_RELIEF_FAST_SPEED));
        dispenseState = DISPENSE_PRESSURE_RELIEF_FAST;
        Serial.println("-> High Viscosity: Fast pressure relief...");
      } else {
        pump.setSpeed(-abs(retractSpeed));
        dispenseState = DISPENSE_RETRACTING;
        Serial.println("-> Standard Retraction...");
      }
      break;

    case DISPENSE_PRESSURE_RELIEF_FAST:
      if (abs(pump.currentPosition()) >= HIGH_VISC_RELIEF_FAST_STEPS) {
        pump.stop();
        pump.setCurrentPosition(0);
        pump.setSpeed(-abs(HIGH_VISC_RELIEF_FINISH_SPEED));
        dispenseState = DISPENSE_PRESSURE_RELIEF_FINISH;
        Serial.println("-> High Viscosity: Controlled relief finish...");
      } else {
        pump.runSpeed();
      }
      break;

    case DISPENSE_PRESSURE_RELIEF_FINISH:
      if (abs(pump.currentPosition()) >= HIGH_VISC_RELIEF_FINISH_STEPS) {
        pump.stop();
        digitalWrite(SHARED_EN, HIGH);
        dispenseState = DISPENSE_COMPLETE;
        Serial.print("Dispense Finished successfully at: ");
        Serial.print(delivered, 2);
        Serial.println("g!");
      } else {
        pump.runSpeed();
      }
      break;

    case DISPENSE_RETRACTING:
      if (abs(pump.currentPosition()) >= retractSteps) {
        pump.stop();
        digitalWrite(SHARED_EN, HIGH);
        dispenseState = DISPENSE_COMPLETE;
        Serial.print("Dispense Finished successfully at: ");
        Serial.print(delivered, 2);
        Serial.println("g!");
      } else {
        pump.runSpeed();
      }
      break;

    case DISPENSE_COMPLETE:
      return true;

    case DISPENSE_ERROR:
      errorLocked = true;
      for (int index = 0; index < PUMP_COUNT; ++index) {
        pumps[index]->stop();
      }
      digitalWrite(SHARED_EN, HIGH);
      return true;

    default:
      break;
  }

  return false;
}

void resetError() {
  errorLocked = false;
  dispenseState = DISPENSE_IDLE;
  Serial.println("ERROR state cleared.");
}

// Halt all motion and report the fault. Emits a TELEMETRY line carrying
// DISPENSE_ERROR so the dashboard detects the fault from structured data
// instead of relying on matching the human-readable log text.
void haltWithError(const char* msg) {
  for (int index = 0; index < PUMP_COUNT; ++index) {
    pumps[index]->stop();
  }
  digitalWrite(SHARED_EN, HIGH);
  mixer.stop();
  Serial.println(msg);
  Serial.print("TELEMETRY:");
  Serial.print(currentWeight, 2);
  Serial.print(",");
  Serial.print((int)DISPENSE_ERROR);
  Serial.println(",0");
  resetRunState();
}

void multipumpSetup() {
  delay(2000);

  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD);

  mixer.begin();

  pinMode(SHARED_DIR, OUTPUT);
  pinMode(SHARED_EN, OUTPUT);
  pinMode(SHARED_MS1, OUTPUT);
  pinMode(SHARED_MS2, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // Start with fan off

  for (int index = 0; index < PUMP_COUNT; ++index) {
    if (pumpStepPins[index] != -1) {
      pinMode(pumpStepPins[index], OUTPUT);
      // Signature: setPinsInverted(directionInvert, stepInvert, enableInvert).
      // Standard pumps use inverted DIR (original behavior); mirrored pumps
      // (2/4/6/8) use non-inverted DIR so positive speed dispenses forward.
      pumps[index]->setPinsInverted(!pumpMirrored(index), false, false);
      pumps[index]->setMaxSpeed(16000);
      pumps[index]->setAcceleration(8000);
    }
  }

  digitalWrite(SHARED_EN, HIGH);
  setMicrostepping(16);

  Serial.print("INFO:PUMPS=");
  Serial.println(PUMP_COUNT);

  Serial.println("\n========================================");
  Serial.println("Multi-Pump Gravimetric Dispensing System");
  Serial.print("Active Pump Count: ");
  Serial.println(PUMP_COUNT);
  Serial.println("Parallel Wiring: Shared EN, DIR, MS1/MS2");
  Serial.println("Step pin configuration:");
  for (int index = 0; index < PUMP_COUNT; ++index) {
    Serial.print("  Pump ");
    Serial.print(index + 1);
    Serial.print(" Step Pin: ");
    Serial.println(pumpStepPins[index]);
  }
  Serial.println("========================================");
  Serial.print("Send 'H1'/'L1' through 'H");
  Serial.print(PUMP_COUNT);
  Serial.print("'/'L");
  Serial.print(PUMP_COUNT);
  Serial.println("' to toggle Glycerol/Water");
  Serial.print("Send 'C1' through 'C");
  Serial.print(PUMP_COUNT);
  Serial.println("' to calibrate an active profile.");
  Serial.println("Send 'CAL SHOW' to print steps/gram; 'CAL SAVE' to persist to flash.");
  Serial.println("Send 'FAN ON' or 'FAN OFF' to turn the DC fan (Pin 22) on/off.");
  Serial.println("Send 'MIX START' / 'MIX STOP' to control the mixer (Step 52, Dir 48, En 50).");
  Serial.println("Send 'MIX RPM <val>' / 'MIX ACCEL <val>' to configure mixer.");
  Serial.println("Send 'MIX STATUS' to show mixer settings and driver diagnostics.");
  Serial.println("Enter target weight > 1.50 for each pump, or 0.0 to skip.");
  Serial.println("========================================");

  calibrationStoreLoad(stepsPerGramLow, stepsPerGramHigh, PUMP_COUNT);
  resetRunState();
}

void multipumpLoop() {
  mixer.update();

  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '+' || c == '-') {
      if (scaleBufIdx > 0) {
        scaleBuffer[scaleBufIdx] = '\0';
        processScaleData(scaleBuffer);
      }
      scaleBufIdx = 0;
      scaleBuffer[0] = c;
      scaleBufIdx = 1;
    } else if (scaleBufIdx < (int)(MAX_BUF - 1)) {
      scaleBuffer[scaleBufIdx++] = c;
    }
  }

  handleUsbCommands();

  if (sequenceState == SEQ_DISPENSE_ACTIVE) {
    if (dispenseState == DISPENSE_BULK_FILL ||
        dispenseState == DISPENSE_TRIM_PULSE ||
        dispenseState == DISPENSE_PRESSURE_RELIEF_FAST ||
        dispenseState == DISPENSE_PRESSURE_RELIEF_FINISH ||
        dispenseState == DISPENSE_RETRACTING ||
        dispenseState == DISPENSE_SETTLE_BULK ||
        dispenseState == DISPENSE_SETTLE_TRIM) {
      if (millis() - lastScaleUpdateTime > 1000) {
        haltWithError("\n!!! ERROR: SCALE TIMEOUT WATCHDOG !!!");
      }
    }
    if (dispenseState == DISPENSE_SETTLE_BULK ||
        dispenseState == DISPENSE_SETTLE_TRIM) {
      if (millis() - stopTime > MAX_SETTLE_TIMEOUT_MS) {
        haltWithError("\n!!! ERROR: SCALE SETTLE TIMEOUT WATCHDOG !!!");
      }
    }
    if (dispenseSequenceStartTime > 0 && (millis() - dispenseSequenceStartTime) > MAX_DISPENSE_TIMEOUT_MS) {
      haltWithError("\n!!! ERROR: MAX DISPENSE TIMEOUT EXCEEDED !!!");
    }
  }

  switch (sequenceState) {
    case SEQ_PROMPT_TARGET:
      if (!promptedTarget && targetEntryPumpIndex < PUMP_COUNT) {
        printTargetPrompt(targetEntryPumpIndex);
        promptedTarget = true;
      }
      break;

    case SEQ_DISPENSE_ACTIVE: {
      if (activePumpIndex >= 0 && activePumpIndex < PUMP_COUNT) {
        float target = targetWeights[activePumpIndex];
        long retractSteps = getRetractSteps(activePumpIndex);
        float retractSpeed = getRetractSpeed(activePumpIndex);

        if (dispensePump(activePumpIndex, target, retractSteps, retractSpeed)) {
          Serial.print("Pump ");
          Serial.print(activePumpIndex + 1);
          Serial.print(" done. Dispensed: ");
          Serial.println(currentWeight - startWeight, 2);

          dispenseState = DISPENSE_IDLE;
          nextPumpIndex = findNextActivePumpAfter(activePumpIndex);

          if (nextPumpIndex < 0) {
            if (!mixingEnabled) {
              Serial.println("\n-> Recipe completed dispensing. Mixing disabled; finishing run.");
              sequenceState = SEQ_DONE;
            } else {
              sequenceState = SEQ_FINAL_MIXING;
              mixerTimer = millis();
              mixer.startContinuous();
              Serial.print("\n-> Recipe completed dispensing. Starting final mixing for ");
              Serial.print(mixFinalDurationMs / 60000.0f, 1);
              Serial.println(" minutes...");
            }
          } else {
            if (!mixingEnabled) {
              Serial.println("\n-> Mixing disabled. Proceeding directly to next pump...");
              activePumpIndex = nextPumpIndex;
              startPumpDispense(activePumpIndex);
            } else {
              sequenceState = SEQ_MIXING_BETWEEN;
              mixerTimer = millis();
              mixer.startContinuous();
              Serial.print("\n-> Starting intermediate mixing for ");
              Serial.print(mixBetweenDurationMs / 60000.0f, 1);
              Serial.println(" minutes...");
            }
          }
        }
      }
      break;
    }

    case SEQ_MIXING_BETWEEN:
      if (millis() - mixerTimer >= mixBetweenDurationMs) {
        mixer.stop();
        sequenceState = SEQ_SETTLE_AFTER_MIX;
        mixerSettleTimer = millis();
        mixerSettleStartCheckTime = 0;
        // Scale settle guard by the *next* pump's viscosity (activePumpIndex
        // is still the pump that just finished).
        int settlePump = (nextPumpIndex >= 0) ? nextPumpIndex : activePumpIndex;
        unsigned long scaledGuardMs = (unsigned long)(mixerSettleGuardMs * getViscosityFactor(settlePump));
        Serial.print("\n-> Intermediate mixing complete. Stopping mixer and waiting ");
        Serial.print(scaledGuardMs / 1000.0f, 1);
        Serial.println(" seconds for liquid to settle...");
      }
      break;

    case SEQ_SETTLE_AFTER_MIX: {
      int settlePump = (nextPumpIndex >= 0) ? nextPumpIndex : activePumpIndex;
      unsigned long scaledGuardMs = (unsigned long)(mixerSettleGuardMs * getViscosityFactor(settlePump));
      if (millis() - mixerSettleTimer < scaledGuardMs) {
        break;
      }

      bool hadNewData = newScaleData;
      newScaleData = false;

      if (!hadNewData) {
        break;
      }

      if (mixerSettleStartCheckTime == 0) {
        mixerSettleStartCheckTime = millis();
        mixerSettleReferenceWeight = currentWeight;
        Serial.println("-> Guard time elapsed. Monitoring scale stability...");
      }

      if (abs(currentWeight - mixerSettleReferenceWeight) > mixerSettleToleranceG) {
        mixerSettleReferenceWeight = currentWeight;
        mixerSettleStartCheckTime = millis();
      } else if (millis() - mixerSettleStartCheckTime >= mixerSettleWindowMs) {
        Serial.print("-> Scale settled at: ");
        Serial.print(currentWeight, 2);
        Serial.println("g. Proceeding to next pump...");

        activePumpIndex = nextPumpIndex;
        sequenceState = SEQ_DISPENSE_ACTIVE;
        startPumpDispense(activePumpIndex);
      }
      break;
    }

    case SEQ_FINAL_MIXING:
      if (millis() - mixerTimer >= mixFinalDurationMs) {
        mixer.stop();
        sequenceState = SEQ_DONE;
      }
      break;

    case SEQ_DONE:
      if (!completionAnnounced) {
        completionAnnounced = true;
        completeRun();
      }
      break;

    case SEQ_CALIBRATE_RUN: {
      if (calibratingPumpIndex >= 0 && calibratingPumpIndex < PUMP_COUNT) {
        AccelStepper& activePump = getPump(calibratingPumpIndex);
        if (activePump.distanceToGo() == 0) {
          activePump.stop();
          activePump.setCurrentPosition(0);
          activePump.setSpeed(-abs(getRetractSpeed(calibratingPumpIndex)));
          sequenceState = SEQ_CALIBRATE_RETRACT;
          Serial.println("-> Calibration dispense complete. Standard Retraction...");
        } else {
          activePump.runSpeedToPosition();
        }
      }
      break;
    }

    case SEQ_CALIBRATE_RETRACT: {
      if (calibratingPumpIndex >= 0 && calibratingPumpIndex < PUMP_COUNT) {
        AccelStepper& activePump = getPump(calibratingPumpIndex);
        long retractSteps = getRetractSteps(calibratingPumpIndex);
        if (abs(activePump.currentPosition()) >= retractSteps) {
          activePump.stop();
          digitalWrite(SHARED_EN, HIGH);

          Serial.println("\n-> Calibration run complete.");
          Serial.print("-> Dispensed ");
          Serial.print(CALIBRATION_RUN_FULL_STEPS);
          Serial.print(" full steps (");
          Serial.print(CALIBRATION_RUN_MICROSTEPS);
          Serial.print(" microsteps) at 1/");
          Serial.print(CALIBRATION_MICROSTEPS);
          Serial.println(" step (bulk-matched speed)...");
          Serial.println("-> Please weigh the dispensed liquid on your scale.");
          Serial.println("-> Enter the measured weight in grams below:");

          sequenceState = SEQ_CALIBRATE_WAIT_INPUT;
          usbBufIdx = 0;
        } else {
          activePump.runSpeed();
        }
      }
      break;
    }

    case SEQ_CALIBRATE_WAIT_INPUT:
      break;

    case SEQ_DROP_CAL: {
      if (!isValidPumpIndex(dropCalPumpIndex)) {
        finishDropCal("invalid_pump");
        break;
      }
      AccelStepper& pump = getPump(dropCalPumpIndex);

      if (dropCalInBurst) {
        if (pump.distanceToGo() != 0) {
          pump.runSpeedToPosition();
        } else {
          dropCalStepsSinceLast += DROP_CAL_BURST_STEPS;
          dropCalTotalSteps += DROP_CAL_BURST_STEPS;
          dropCalInBurst = false;
          dropCalSettleStart = millis();
        }
        break;
      }

      // Settle between bursts so the scale can show a detached drop.
      if (millis() - dropCalSettleStart < DROP_CAL_SETTLE_MS) {
        break;
      }
      if (!newScaleData) {
        break;
      }
      newScaleData = false;

      float delta = currentWeight - dropCalLastWeight;
      float sampleDelta = currentWeight - dropCalPrevSample;
      dropCalPrevSample = currentWeight;

      // Continuous stream / cup placement: re-baseline, discard steps.
      if (delta > DROP_CAL_MAX_DROP_G) {
        Serial.print("DROP_REJECT:pump=");
        Serial.print(dropCalPumpIndex + 1);
        Serial.print(",delta_g=");
        Serial.print(delta, 4);
        Serial.println(" (stream/oversize)");
        dropCalLastWeight = currentWeight;
        dropCalStepsSinceLast = 0;
        dropCalSettleStart = millis();
        break;
      }

      // Scale still moving: wait. Keep baseline so partial mass can accumulate
      // across samples (critical at 0.01 g scale resolution).
      if (fabsf(sampleDelta) >= DROP_CAL_STABLE_G) {
        dropCalSettleStart = millis();
        break;
      }

      if (delta >= DROP_CAL_DETECT_G && delta <= DROP_CAL_MAX_DROP_G) {
        if (dropCalRecorded < DROP_CAL_MAX_EVENTS) {
          dropCalMasses[dropCalRecorded] = delta;
          dropCalStepsArr[dropCalRecorded] = dropCalStepsSinceLast;
          Serial.print("DROP_EVENT:pump=");
          Serial.print(dropCalPumpIndex + 1);
          Serial.print(",idx=");
          Serial.print(dropCalRecorded + 1);
          Serial.print(",steps=");
          Serial.print(dropCalStepsSinceLast);
          Serial.print(",mass_g=");
          Serial.println(delta, 4);
          dropCalRecorded++;
        }
        dropCalLastWeight = currentWeight;
        dropCalStepsSinceLast = 0;

        if (dropCalRecorded >= dropCalTargetCount) {
          finishDropCal("complete");
          break;
        }
        // After a real drop, pause before next burst so meniscus can reform.
        dropCalSettleStart = millis();
        break;
      }

      if (dropCalTotalSteps >= DROP_CAL_MAX_TOTAL_STEPS) {
        finishDropCal("max_steps");
        break;
      }

      // Stable but under detect threshold: inchworm more liquid.
      startDropCalBurst();
      break;
    }

    case SEQ_MAINTENANCE_RUN: {
      bool anyActive = false;
      unsigned long now = millis();

      for (int i = 0; i < PUMP_COUNT; ++i) {
        if (!maintenancePumpActive[i]) continue;

        if (now >= maintenancePumpEndTime[i]) {
          pumps[i]->stop();
          maintenancePumpActive[i] = false;
          maintenanceSpeed[i] = 0.0f;
          maintenancePumpEndTime[i] = 0;
        } else {
          pumps[i]->setSpeed(maintenanceSpeed[i]);
          pumps[i]->runSpeed();
          anyActive = true;
        }
      }

      if (!anyActive) {
        // Phase finished: start next phase (odd->even) if pending; otherwise finish.
        if (!startNextMaintenancePhase(now)) {
          digitalWrite(SHARED_EN, HIGH);
          sequenceState = SEQ_PROMPT_TARGET;
          promptedTarget = false;
          Serial.println("INFO:Maintenance run complete.");
        }
      }
      break;
    }
  }
}

void processScaleData(const char* raw) {
  char cleanStr[64];
  int cleanIdx = 0;
  for (int index = 0; raw[index] != '\0'; index++) {
    char ch = raw[index];
    if (isDigit(ch) || ch == '.' || ch == '-') {
      if (cleanIdx < 63) cleanStr[cleanIdx++] = ch;
    }
  }
  cleanStr[cleanIdx] = '\0';

  if (cleanIdx > 0) {
    rawWeight = strtof(cleanStr, nullptr);
    currentWeight = rawWeight - tareOffset;
    lastScaleUpdateTime = millis();
    newScaleData = true;

    if (dispenseState == DISPENSE_BULK_FILL || dispenseState == DISPENSE_TRIM_PULSE) {
      updateFillRate();
    }

    int telemetryPumpIndex = activePumpIndex;
    if (!isValidPumpIndex(telemetryPumpIndex)) {
      telemetryPumpIndex = targetEntryPumpIndex;
    }
    if (!isValidPumpIndex(telemetryPumpIndex)) {
      telemetryPumpIndex = 0;
    }

    Serial.print("TELEMETRY:");
    Serial.print(currentWeight, 2);
    Serial.print(",");
    Serial.print((int)dispenseState);
    Serial.print(",");
    Serial.println(highViscosity[telemetryPumpIndex] ? "1" : "0");
  }
}

void handleUsbCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (usbBufIdx > 0) {
        usbBuffer[usbBufIdx] = '\0';
        strTrim(usbBuffer);

        if (strEqualsIgnoreCase(usbBuffer, "S")) {
          for (int index = 0; index < PUMP_COUNT; ++index) {
            pumps[index]->stop();
          }
          digitalWrite(SHARED_EN, HIGH);
          mixer.stop();
          Serial.println("!!! EMERGENCY PUMP & MIXER HALT !!!");
          resetError();
          resetRunState();
        }
        else if (strEqualsIgnoreCase(usbBuffer, "CAL SHOW") ||
                 strEqualsIgnoreCase(usbBuffer, "CAL_SHOW") ||
                 strEqualsIgnoreCase(usbBuffer, "GET CAL")) {
          calibrationStorePrint(stepsPerGramLow, stepsPerGramHigh, PUMP_COUNT);
        }
        else if (strEqualsIgnoreCase(usbBuffer, "CAL SAVE") ||
                 strEqualsIgnoreCase(usbBuffer, "CAL_SAVE")) {
          if (calibrationStoreSave(stepsPerGramLow, stepsPerGramHigh, PUMP_COUNT)) {
            Serial.println("CAL_SAVE: OK");
          } else {
            Serial.println("CAL_SAVE: FAILED");
          }
        }
        else if (strEqualsIgnoreCase(usbBuffer, "MIX START") || strEqualsIgnoreCase(usbBuffer, "MIXER START") || strEqualsIgnoreCase(usbBuffer, "MIX ON")) {
          mixer.startContinuous();
        }
        else if (strEqualsIgnoreCase(usbBuffer, "MIX STOP") || strEqualsIgnoreCase(usbBuffer, "MIXER STOP") || strEqualsIgnoreCase(usbBuffer, "MIX OFF")) {
          mixer.stop();
        }
        else if (strEqualsIgnoreCase(usbBuffer, "MIX ENABLE")) {
          mixingEnabled = true;
          Serial.println("Mixing module: ENABLED");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "MIX DISABLE")) {
          mixingEnabled = false;
          mixer.stop();
          Serial.println("Mixing module: DISABLED (recipes will skip mixing phases)");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "ORDER DEFAULT") ||
                 strEqualsIgnoreCase(usbBuffer, "ORDER RESET")) {
          resetDispenseOrder();
          Serial.println("Dispense order reset to pump 1 through pump count.");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "ORDER ")) {
          if (applyDispenseOrderCommand(usbBuffer + 6)) {
            Serial.print("Dispense order set: ");
            for (int orderIndex = 0; orderIndex < PUMP_COUNT; ++orderIndex) {
              Serial.print(dispenseOrder[orderIndex] + 1);
              if (orderIndex < PUMP_COUNT - 1) Serial.print(",");
            }
            Serial.println();
          } else {
            Serial.print("ERROR: Invalid dispense order. Send ORDER with ");
            Serial.print(PUMP_COUNT);
            Serial.println(" unique pump numbers.");
          }
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX RPM ") || strStartsWithIgnoreCase(usbBuffer, "MIXER RPM ")) {
          const char* offsetPtr = strStartsWithIgnoreCase(usbBuffer, "MIXER RPM ") ? usbBuffer + 10 : usbBuffer + 8;
          float rpm = strtof(offsetPtr, nullptr);
          mixer.setTargetRPM(rpm);
          Serial.print("Mixer Target RPM set to: ");
          Serial.println(mixer.getTargetRPM());
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX ACCEL ") || strStartsWithIgnoreCase(usbBuffer, "MIXER ACCEL ")) {
          const char* offsetPtr = strStartsWithIgnoreCase(usbBuffer, "MIXER ACCEL ") ? usbBuffer + 12 : usbBuffer + 10;
          float accel = strtof(offsetPtr, nullptr);
          mixer.setAcceleration(accel);
          Serial.print("Mixer Acceleration set to: ");
          Serial.print(mixer.getAcceleration());
          Serial.println(" steps/sec^2");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX TIME BETWEEN ")) {
          float seconds = strtof(usbBuffer + 17, nullptr);
          mixBetweenDurationMs = (unsigned long)(seconds * 1000.0f);
          Serial.print("Intermediate mixing duration set to: ");
          Serial.print(seconds, 1);
          Serial.println("s");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX TIME FINAL ")) {
          float seconds = strtof(usbBuffer + 15, nullptr);
          mixFinalDurationMs = (unsigned long)(seconds * 1000.0f);
          Serial.print("Final mixing duration set to: ");
          Serial.print(seconds, 1);
          Serial.println("s");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX SETTLE GUARD ")) {
          float seconds = strtof(usbBuffer + 17, nullptr);
          mixerSettleGuardMs = (unsigned long)(seconds * 1000.0f);
          Serial.print("Mixer settle guard time set to: ");
          Serial.print(seconds, 1);
          Serial.println("s");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX SETTLE WINDOW ")) {
          float seconds = strtof(usbBuffer + 18, nullptr);
          mixerSettleWindowMs = (unsigned long)(seconds * 1000.0f);
          Serial.print("Mixer settle window time set to: ");
          Serial.print(seconds, 1);
          Serial.println("s");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "MIX SETTLE TOL ")) {
          float tolerance = strtof(usbBuffer + 15, nullptr);
          mixerSettleToleranceG = tolerance;
          Serial.print("Mixer settle tolerance set to: ");
          Serial.print(tolerance, 3);
          Serial.println("g");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "MIX STATUS") || strEqualsIgnoreCase(usbBuffer, "MIXER STATUS")) {
          Serial.println("\n--- INTEGRATED MIXER STATUS ---");
          Serial.print("Mix Module:   ");
          Serial.println(mixingEnabled ? "ENABLED" : "DISABLED");
          Serial.print("State:        ");
          Serial.println(mixer.getStateString());
          Serial.print("Target Speed: ");
          Serial.print(mixer.getTargetRPM());
          Serial.println(" RPM");
          Serial.print("Acceleration: ");
          Serial.print(mixer.getAcceleration());
          Serial.println(" steps/sec^2");
          Serial.print("Auto-Ramping: ");
          Serial.println(mixer.isAutoRamping() ? "Active" : "Inactive");
          Serial.print("Mix Between:  ");
          Serial.print(mixBetweenDurationMs / 1000.0f, 1);
          Serial.println(" s");
          Serial.print("Mix Final:    ");
          Serial.print(mixFinalDurationMs / 1000.0f, 1);
          Serial.println(" s");
          Serial.print("Settle Guard: ");
          Serial.print(mixerSettleGuardMs / 1000.0f, 1);
          Serial.println(" s");
          Serial.print("Settle Window:");
          Serial.print(mixerSettleWindowMs / 1000.0f, 1);
          Serial.println(" s");
          Serial.print("Settle Tol:   ");
          Serial.print(mixerSettleToleranceG, 3);
          Serial.println(" g");

          bool uartConnected = mixer.checkUARTConnection();
          Serial.print("Driver UART:  ");
          if (uartConnected) {
              Serial.println("ONLINE (SpreadCycle Active)");
          } else {
              Serial.println("OFFLINE (StealthChop Standalone Mode)");
          }
          Serial.print("Raw GCONF:    0x");
          Serial.println(mixer.getGCONF(), HEX);
          Serial.print("Raw IOIN:     0x");
          Serial.println(mixer.getIOIN(), HEX);
          if (uartConnected) {
              Serial.print("Driver Current: ");
              Serial.print(mixer.getDriverCurrent());
              Serial.println(" mA RMS");
              Serial.print("Driver Microsteps: ");
              Serial.println(mixer.getDriverMicrosteps());
          }
          Serial.println("--------------------------------");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "GET INFO") || strEqualsIgnoreCase(usbBuffer, "INFO")) {
          Serial.print("INFO:PUMPS=");
          Serial.println(PUMP_COUNT);
          Serial.print("INFO:VISC=");
          for (int i = 0; i < PUMP_COUNT; ++i) {
            Serial.print(pumpViscosity[i], 1);
            if (i < PUMP_COUNT - 1) Serial.print(",");
          }
          Serial.println();
          Serial.print("INFO:MIXEN=");
          Serial.println(mixingEnabled ? "1" : "0");
          Serial.println("INFO:ORDER=1");
          Serial.println("INFO:TARGET=1");
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "VISC ")) {
          int pumpNum = 0;
          float cP = 1.0f;
          char* space1 = strchr(usbBuffer + 5, ' ');
          if (space1 != nullptr) {
            pumpNum = atoi(usbBuffer + 5);
            cP = strtof(space1 + 1, nullptr);
            int pIdx = pumpNum - 1;
            if (isValidPumpIndex(pIdx)) {
              pumpViscosity[pIdx] = cP;
              highViscosity[pIdx] = (cP >= 100.0f);
              Serial.print("Pump ");
              Serial.print(pumpNum);
              Serial.print(" Viscosity set to: ");
              Serial.print(cP, 1);
              Serial.println(" cP");
            } else {
              Serial.println("ERROR: Invalid pump index.");
            }
          }
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "TARGET ")) {
          char* args = usbBuffer + 7;
          char* space1 = strchr(args, ' ');
          if (space1 == nullptr) {
            Serial.println("ERROR: TARGET command must be TARGET <pump> <grams>.");
          } else {
            *space1 = '\0';
            int pumpNumber = atoi(args);
            char* valueText = space1 + 1;
            while (*valueText == ' ') valueText++;
            float target = strtof(valueText, nullptr);
            bool isZero = (strcmp(valueText, "0") == 0 ||
                           strcmp(valueText, "0.0") == 0 ||
                           strcmp(valueText, "0.00") == 0);
            if (sequenceState == SEQ_PROMPT_TARGET &&
                setTargetForPump(pumpNumber - 1, target, isZero) &&
                ((targetReceivedMask & allPumpTargetsMask()) == allPumpTargetsMask())) {
              startFirstActivePump();
            }
          }
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "PRIME ")) {
          int bitmask = 0;
          int seconds = 0;
          float speed = 0.0f;
          int dir = 1;
          
          char* token = strtok(usbBuffer + 6, " ");
          if (token != nullptr) bitmask = atoi(token);
          token = strtok(nullptr, " ");
          if (token != nullptr) seconds = atoi(token);
          token = strtok(nullptr, " ");
          if (token != nullptr) speed = strtof(token, nullptr);
          token = strtok(nullptr, " ");
          if (token != nullptr) dir = atoi(token);
          // Prepare 2-phase run (odd group then even group) to keep shared DIR coherent.
          maintenanceModePrime = true;
          maintenanceDir = (dir >= 0) ? 1 : -1;
          maintenanceSeconds = seconds;
          maintenanceSpeedOverride = speed;

          const uint32_t oddMask = getOddGroupMask();
          const uint32_t evenMask = (~oddMask) & ((PUMP_COUNT >= 32) ? 0xFFFFFFFFUL : ((1UL << PUMP_COUNT) - 1UL));

          maintenancePendingOddMask = ((uint32_t)bitmask) & oddMask;
          maintenancePendingEvenMask = ((uint32_t)bitmask) & evenMask;

          unsigned long now = millis();
          if (maintenancePendingOddMask == 0 && maintenancePendingEvenMask == 0) {
            Serial.println("ERROR: No valid pumps in prime mask.");
            break;
          }

          Serial.print("Starting parallel Prime/Purge for mask ");
          Serial.print(bitmask);
          Serial.print(" (dir=");
          Serial.print(dir);
          Serial.println(") with per-pump durations...");

          // Start phase 1 (odd) or phase 2 (even) immediately.
          if (!startNextMaintenancePhase(now)) {
            Serial.println("ERROR: Unable to start maintenance phase.");
            break;
          }
        }
        else if (strStartsWithIgnoreCase(usbBuffer, "FLUSH ")) {
          int bitmask = 0;
          int seconds = 0;
          float speed = 0.0f;
          
          char* token = strtok(usbBuffer + 6, " ");
          if (token != nullptr) bitmask = atoi(token);
          token = strtok(nullptr, " ");
          if (token != nullptr) seconds = atoi(token);
          token = strtok(nullptr, " ");
          if (token != nullptr) speed = strtof(token, nullptr);

          int maxViscIndex = -1;
          float maxVisc = 0.0f;
          
          for (int i = 0; i < PUMP_COUNT; ++i) {
            if ((bitmask & (1 << i)) != 0 && pumpStepPins[i] != -1) {
              if (pumpViscosity[i] > maxVisc) {
                maxVisc = pumpViscosity[i];
                maxViscIndex = i;
              }
            }
          }
          
          float viscFactor = 1.0f;
          if (maxViscIndex != -1) {
            viscFactor = getViscosityFactor(maxViscIndex);
          }
          
          if (seconds <= 0) {
            seconds = (int)(BASE_FLUSH_TIME_SEC * viscFactor);
          }

          // Prepare 2-phase run (odd group then even group) to keep shared DIR coherent.
          maintenanceModePrime = false;
          maintenanceDir = 1;
          maintenanceSeconds = seconds;
          maintenanceSpeedOverride = speed;

          const uint32_t oddMask = getOddGroupMask();
          const uint32_t evenMask = (~oddMask) & ((PUMP_COUNT >= 32) ? 0xFFFFFFFFUL : ((1UL << PUMP_COUNT) - 1UL));

          maintenancePendingOddMask = ((uint32_t)bitmask) & oddMask;
          maintenancePendingEvenMask = ((uint32_t)bitmask) & evenMask;

          unsigned long now = millis();
          if (maintenancePendingOddMask == 0 && maintenancePendingEvenMask == 0) {
            Serial.println("ERROR: No valid pumps in flush mask.");
            break;
          }

          Serial.print("Starting parallel Flush for mask ");
          Serial.print(bitmask);
          Serial.print(" for ");
          Serial.print(seconds);
          Serial.println("s...");

          // Start phase 1 (odd) or phase 2 (even) immediately.
          if (!startNextMaintenancePhase(now)) {
            Serial.println("ERROR: Unable to start maintenance phase.");
            break;
          }
        }
        else if (strEqualsIgnoreCase(usbBuffer, "T")) {
          tareOffset = rawWeight;
          currentWeight = 0.0f;
          Serial.println("Scale Software Tared.");
        }
        else if (sequenceState == SEQ_PROMPT_TARGET &&
                 (strStartsWithIgnoreCase(usbBuffer, "DROP CAL ") ||
                  strStartsWithIgnoreCase(usbBuffer, "DROPCAL "))) {
          const char* args = strStartsWithIgnoreCase(usbBuffer, "DROPCAL ")
            ? usbBuffer + 8
            : usbBuffer + 9;
          while (*args == ' ') args++;
          int pumpNumber = atoi(args);
          int pumpIndex = pumpNumber - 1;
          int dropCount = DROP_CAL_DEFAULT_COUNT;
          const char* second = args;
          while (*second && *second != ' ') second++;
          while (*second == ' ') second++;
          if (*second) {
            int parsed = atoi(second);
            if (parsed > 0) dropCount = parsed;
          }
          if (!isValidPumpIndex(pumpIndex)) {
            Serial.print("ERROR: DROP CAL pump must be 1 through ");
            Serial.print(PUMP_COUNT);
            Serial.println(".");
          } else {
            startDropCal(pumpIndex, dropCount);
          }
        }
        else if (strEqualsIgnoreCase(usbBuffer, "FAN ON") || strEqualsIgnoreCase(usbBuffer, "FAN_ON") || strEqualsIgnoreCase(usbBuffer, "FON")) {
          digitalWrite(FAN_PIN, HIGH);
          Serial.println("DC Fan: ON");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "FAN OFF") || strEqualsIgnoreCase(usbBuffer, "FAN_OFF") || strEqualsIgnoreCase(usbBuffer, "FOFF")) {
          digitalWrite(FAN_PIN, LOW);
          Serial.println("DC Fan: OFF");
        }
        else if ((usbBuffer[0] == 'H' || usbBuffer[0] == 'h' ||
                  usbBuffer[0] == 'L' || usbBuffer[0] == 'l') &&
                 isDigit(usbBuffer[1])) {
          int pumpNumber = atoi(usbBuffer + 1);
          int pumpIndex = pumpNumber - 1;
          if (isValidPumpIndex(pumpIndex)) {
            highViscosity[pumpIndex] = (usbBuffer[0] == 'H' || usbBuffer[0] == 'h');
            pumpViscosity[pumpIndex] = highViscosity[pumpIndex] ? 1000.0f : 1.0f;
            Serial.print("Pump ");
            Serial.print(pumpNumber);
            Serial.print(" Viscosity Mode: ");
            Serial.println(highViscosity[pumpIndex] ? "HIGH (Glycerol)" : "LOW (Water)");
          } else {
            Serial.print("Error: Pump index must be 1 through ");
            Serial.print(PUMP_COUNT);
            Serial.println(".");
          }
        }
        else if (sequenceState == SEQ_PROMPT_TARGET &&
                 (usbBuffer[0] == 'C' || usbBuffer[0] == 'c')) {
          if (strlen(usbBuffer) > 1 && isDigit(usbBuffer[1])) {
            int pumpNumber = atoi(usbBuffer + 1);
            int pumpIndex = pumpNumber - 1;
            if (isValidPumpIndex(pumpIndex)) {
              calibratingPumpIndex = pumpIndex;
            } else {
              Serial.print("Error: Pump index must be 1 through ");
              Serial.print(PUMP_COUNT);
              Serial.println(".");
              usbBufIdx = 0;
              return;
            }
          } else {
            calibratingPumpIndex = targetEntryPumpIndex;
          }

          digitalWrite(SHARED_EN, LOW);
          activeMicrosteps = CALIBRATION_MICROSTEPS;
          setMicrostepping(activeMicrosteps);

          AccelStepper& activePump = getPump(calibratingPumpIndex);
          float calSpeed = BULK_SPEED / getViscosityFactor(calibratingPumpIndex);
          activePump.setCurrentPosition(0);
          activePump.moveTo(CALIBRATION_RUN_MICROSTEPS);
          activePump.setSpeed(calSpeed);

          sequenceState = SEQ_CALIBRATE_RUN;
          Serial.print("\n-> Starting calibration run for Pump ");
          Serial.println(calibratingPumpIndex + 1);
          Serial.print("-> Dispensing exactly ");
          Serial.print(CALIBRATION_RUN_FULL_STEPS);
          Serial.print(" full steps (");
          Serial.print(CALIBRATION_RUN_MICROSTEPS);
          Serial.print(" microsteps) at 1/");
          Serial.print(CALIBRATION_MICROSTEPS);
          Serial.print(" step, speed ");
          Serial.print(calSpeed, 0);
          Serial.println(" (matches bulk)...");
        }
        else if (sequenceState == SEQ_CALIBRATE_WAIT_INPUT) {
          float measuredWeight = strtof(usbBuffer, nullptr);
          if (measuredWeight > 0.02f) {
            float fullStepsTaken = (float)CALIBRATION_RUN_FULL_STEPS;
            float calculatedSteps = fullStepsTaken / measuredWeight;
            float microstepsPerGram = (float)CALIBRATION_RUN_MICROSTEPS / measuredWeight;

            if (highViscosity[calibratingPumpIndex]) {
              stepsPerGramHigh[calibratingPumpIndex] = calculatedSteps;
            } else {
              stepsPerGramLow[calibratingPumpIndex] = calculatedSteps;
            }

            Serial.println("\n========================================");
            Serial.println("Calibration Successful!");
            Serial.print("Pump ");
            Serial.print(calibratingPumpIndex + 1);
            Serial.print(" Measured weight: ");
            Serial.print(measuredWeight, 2);
            Serial.println("g");
            Serial.print("New steps/gram [");
            Serial.print(highViscosity[calibratingPumpIndex] ? "Glycerol" : "Water");
            Serial.print("]: ");
            Serial.println(calculatedSteps, 2);
            Serial.print("Calibration math: ");
            Serial.print(CALIBRATION_RUN_FULL_STEPS);
            Serial.print(" full steps; ");
            Serial.print(fullStepsTaken, 1);
            Serial.print(" / ");
            Serial.print(measuredWeight, 3);
            Serial.print("g = ");
            Serial.print(calculatedSteps, 2);
            Serial.print(" full steps/g (");
            Serial.print(microstepsPerGram, 2);
            Serial.println(" microsteps/g at calibration resolution).");
            if (calibrationStoreSave(stepsPerGramLow, stepsPerGramHigh, PUMP_COUNT)) {
              Serial.println("Calibration persisted to flash.");
            }
            Serial.println("========================================");

            sequenceState = SEQ_PROMPT_TARGET;
            promptedTarget = false;
          } else {
            Serial.println("ERROR: Invalid measured weight. Must be > 0.02g:");
          }
        }
        else {
          float target = strtof(usbBuffer, nullptr);
          bool isZero = (strcmp(usbBuffer, "0") == 0 || strcmp(usbBuffer, "0.0") == 0 || strcmp(usbBuffer, "0.00") == 0);

          if (sequenceState == SEQ_PROMPT_TARGET && (isZero || target > 1.5f)) {
            int pumpIndex = targetEntryPumpIndex;
            setTargetForPump(pumpIndex, target, isZero);

            if (targetEntryPumpIndex < PUMP_COUNT - 1) {
              targetEntryPumpIndex++;
              promptedTarget = false;
            } else {
              startFirstActivePump();
            }
          } else if (sequenceState == SEQ_PROMPT_TARGET) {
            Serial.println("ERROR: Invalid target. Must be > 1.50g (or 0.00g to skip).");
          }
        }

        usbBufIdx = 0;
      }
    } else if (usbBufIdx < (int)(MAX_BUF - 1)) {
      usbBuffer[usbBufIdx++] = c;
    }
  }
}

} // namespace MultiPump
