#include "Multipump_dispensing.h"
#include "config.h"
#include "Mixer.h"

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
const float CALIBRATE_SPEED = 2000.0f;
const long retractStepsWater = 3200;
const long retractStepsGlycerol = 9600;
const float DISPENSE_TOLERANCE_G = 0.01f;
const float LOW_VISC_STOP_LEAD_G = 0.01f;
const float HIGH_VISC_STOP_LEAD_G = 0.10f;
const unsigned long LOW_VISC_SETTLE_GUARD_MS = 1200;
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

float stepsPerGramLow[MAX_PUMP_COUNT] = {700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f, 700.0f};
float stepsPerGramHigh[MAX_PUMP_COUNT] = {1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f, 1400.0f};

bool highViscosity[MAX_PUMP_COUNT] = {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
float pumpViscosity[MAX_PUMP_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

// --- Maintenance Sequence State ---
unsigned long maintenancePumpEndTime[MAX_PUMP_COUNT] = {0};
bool maintenancePumpActive[MAX_PUMP_COUNT] = {false};
float maintenanceSpeed[MAX_PUMP_COUNT] = {0.0f};

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
  float minMargin = 1.5f;
  float maxMargin = (pumpViscosity[pumpIndex] >= 100.0f) ? 20.0f : 10.0f;
  
  float trimMargin = targetG * 0.02f * factor;
  if (trimMargin < minMargin) trimMargin = minMargin;
  if (trimMargin > maxMargin) trimMargin = maxMargin;
  
  float bulkFraction = 1.0f - (trimMargin / targetG);
  if (bulkFraction < 0.10f) bulkFraction = 0.10f; // Safety clamp
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
  return (pumpViscosity[pumpIndex] >= 100.0f) ? -16000.0f : -1200.0f;
}

float getStopLeadG(int pumpIndex) {
  const float BASE_STOP_LEAD_G = 0.15f;
  return BASE_STOP_LEAD_G * getViscosityFactor(pumpIndex);
}

int findNextActivePump(int startIndex) {
  for (int index = startIndex; index < PUMP_COUNT; ++index) {
    if (targetWeights[index] > 0.0f) {
      return index;
    }
  }
  return -1;
}

void resetRunState() {
  for (int index = 0; index < PUMP_COUNT; ++index) {
    targetWeights[index] = 0.0f;
  }

  targetEntryPumpIndex = 0;
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
  unsigned long baseGuardTime = 500; // Baseline settle guard (0.5s)
  unsigned long settleGuardTime = (unsigned long)(baseGuardTime * factor);

  if (millis() - stopTime < settleGuardTime) {
    return false;
  }

  // Clear flag immediately so new readings can arrive
  bool hadNewData = newScaleData;
  newScaleData = false;

  if (!hadNewData) {
    return false;
  }

  unsigned long scaledSettleTimeout = (unsigned long)(SETTLE_TIMEOUT_MS * factor);
  if (millis() - settleTimer >= scaledSettleTimeout) {
    if (abs(currentWeight - lastSettleWeight) < 0.005f) {
      return true;
    }
    lastSettleWeight = currentWeight;
    settleTimer = millis();
  }
  return false;
}

void setupBulkFillHelper(int pumpIndex, float targetG) {
  AccelStepper& pump = getPump(pumpIndex);

  startWeight = currentWeight;
  lastScaleUpdateTime = millis();

  float adjustedTarget = targetG - getStopLeadG(pumpIndex);
  float bulkFraction = getDynamicBulkFraction(pumpIndex, targetG);
  float bulkTargetG = adjustedTarget * bulkFraction;
  float stepsPerGram = getStepsPerGram(pumpIndex);

  activeMicrosteps = 8;
  setMicrostepping(activeMicrosteps);

  bulkEndSteps = (bulkTargetG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(BULK_SPEED / getViscosityFactor(pumpIndex));

  digitalWrite(SHARED_EN, LOW);
  bulkStartTime = millis();
  dispenseState = DISPENSE_BULK_FILL;
}

void prepareTrimPulseHelper(int pumpIndex, float remaining) {
  AccelStepper& pump = getPump(pumpIndex);
  bool isHighVisc = highViscosity[pumpIndex];
  float stepsPerGram = getStepsPerGram(pumpIndex);

  dispenseState = DISPENSE_TRIM_PULSE;
  activeMicrosteps = isHighVisc ? 16 : 64;
  setMicrostepping(activeMicrosteps);

  float factor = getViscosityFactor(pumpIndex);
  float coefficient;
  if (remaining > 0.50f) {
    coefficient = 0.80f / factor;
  } else if (remaining > 0.15f) {
    coefficient = (isHighVisc ? 0.50f : 0.60f) / factor;
  } else {
    coefficient = (isHighVisc ? 0.15f : 0.40f) / factor;
  }

  float minPulseG = 0.005f * factor;
  float pulseG = max(minPulseG, remaining * coefficient);
  pulseG = min(pulseG, remaining);

  trimStepsRemaining = (pulseG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(TRICKLE_SPEED / factor);

  Serial.print("-> Micro-Pulse: +");
  Serial.print(pulseG, 3);
  Serial.println("g");
}

void startPumpDispense(int pumpIndex) {
  float targetG = targetWeights[pumpIndex];
  float adjustedTarget = targetG - getStopLeadG(pumpIndex);

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
  Serial.print("-> Aiming for ");
  Serial.print(adjustedTarget * getDynamicBulkFraction(pumpIndex, targetG), 2);
  Serial.print("g bulk fill (");
  Serial.print(bulkEndSteps);
  Serial.println(" steps at 1/8 microstep)");
}

void startFirstActivePump() {
  int firstActivePump = findNextActivePump(0);
  if (firstActivePump < 0) {
    Serial.println("All targets set to 0.00g. Dispensing skipped.");
    resetRunState();
    return;
  }

  Serial.println("Starting Sequential Dispense.");
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
  float motorStopTarget = targetVal - getStopLeadG(pumpIndex);

  switch (dispenseState) {
    case DISPENSE_IDLE:
      break;

    case DISPENSE_BULK_FILL:
      if (delivered >= motorStopTarget) {
        pump.stop();
        stopTime = millis();

        activeMicrosteps = isHighVisc ? 16 : 64;
        setMicrostepping(activeMicrosteps);

        Serial.print("-> Target reached during Bulk Fill (Delivered: ");
        Serial.print(delivered, 2);
        Serial.println("g). Halting to Settle...");

        dispenseState = DISPENSE_SETTLE_BULK;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else if (pump.currentPosition() >= bulkEndSteps) {
        pump.stop();
        stopTime = millis();

        activeMicrosteps = isHighVisc ? 16 : 64;
        setMicrostepping(activeMicrosteps);

        dispenseState = DISPENSE_SETTLE_BULK;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
        Serial.println("-> Bulk Fill complete. Settling scale...");
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

    case DISPENSE_SETTLE_BULK:
      if (isScaleSettled(pumpIndex)) {
        Serial.print("-> Bulk Settled at: ");
        Serial.print(delivered, 2);
        Serial.println("g");

        if (remaining <= DISPENSE_TOLERANCE_G) {
          Serial.print("-> Remaining weight <= ");
          Serial.print(DISPENSE_TOLERANCE_G, 2);
          Serial.println("g. Halting dispense.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else if (delivered < targetVal) {
          prepareTrimPulseHelper(pumpIndex, remaining);
        } else {
          dispenseState = DISPENSE_SUCK_BACK;
        }
      }
      break;

    case DISPENSE_TRIM_PULSE:
      if (delivered >= motorStopTarget) {
        pump.stop();
        stopTime = millis();

        Serial.print("-> Target reached during Trim Pulse (Delivered: ");
        Serial.print(delivered, 2);
        Serial.println("g). Halting to Settle...");

        dispenseState = DISPENSE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else if (pump.currentPosition() >= trimStepsRemaining) {
        pump.stop();
        stopTime = millis();
        dispenseState = DISPENSE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else {
        pump.runSpeed();
      }
      break;

    case DISPENSE_SETTLE_TRIM:
      if (isScaleSettled(pumpIndex)) {
        if (remaining <= DISPENSE_TOLERANCE_G) {
          Serial.print("-> Remaining weight <= ");
          Serial.print(DISPENSE_TOLERANCE_G, 2);
          Serial.println("g. Halting dispense.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else if (delivered < targetVal) {
          prepareTrimPulseHelper(pumpIndex, remaining);
        } else {
          dispenseState = DISPENSE_SUCK_BACK;
        }
      }
      break;

    case DISPENSE_SUCK_BACK:
      pump.setCurrentPosition(0);
      if (isHighVisc) {
        pump.setSpeed(HIGH_VISC_RELIEF_FAST_SPEED);
        dispenseState = DISPENSE_PRESSURE_RELIEF_FAST;
        Serial.println("-> High Viscosity: Fast pressure relief...");
      } else {
        pump.setSpeed(retractSpeed);
        dispenseState = DISPENSE_RETRACTING;
        Serial.println("-> Standard Retraction...");
      }
      break;

    case DISPENSE_PRESSURE_RELIEF_FAST:
      if (abs(pump.currentPosition()) >= HIGH_VISC_RELIEF_FAST_STEPS) {
        pump.stop();
        pump.setCurrentPosition(0);
        pump.setSpeed(HIGH_VISC_RELIEF_FINISH_SPEED);
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
      pumps[index]->setPinsInverted(true, false, false);
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
  Serial.println("Send 'FAN ON' or 'FAN OFF' to turn the DC fan (Pin 22) on/off.");
  Serial.println("Send 'MIX START' / 'MIX STOP' to control the mixer (Step 52, Dir 48, En 50).");
  Serial.println("Send 'MIX RPM <val>' / 'MIX ACCEL <val>' to configure mixer.");
  Serial.println("Send 'MIX STATUS' to show mixer settings and driver diagnostics.");
  Serial.println("Enter target weight > 1.50 for each pump, or 0.0 to skip.");
  Serial.println("========================================");

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
          nextPumpIndex = findNextActivePump(activePumpIndex + 1);

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
        unsigned long scaledGuardMs = (unsigned long)(mixerSettleGuardMs * getViscosityFactor(activePumpIndex));
        Serial.print("\n-> Intermediate mixing complete. Stopping mixer and waiting ");
        Serial.print(scaledGuardMs / 1000.0f, 1);
        Serial.println(" seconds for liquid to settle...");
      }
      break;

    case SEQ_SETTLE_AFTER_MIX: {
      unsigned long scaledGuardMs = (unsigned long)(mixerSettleGuardMs * getViscosityFactor(activePumpIndex));
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
          digitalWrite(SHARED_EN, HIGH);

          Serial.println("\n-> Calibration run complete (10000 steps).");
          Serial.println("-> Please weigh the dispensed liquid on your scale.");
          Serial.println("-> Enter the measured weight in grams (e.g. 1.29) below:");

          sequenceState = SEQ_CALIBRATE_WAIT_INPUT;
          usbBufIdx = 0;
        } else {
          activePump.runSpeedToPosition();
        }
      }
      break;
    }

    case SEQ_CALIBRATE_WAIT_INPUT:
      break;

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
        digitalWrite(SHARED_EN, HIGH);
        sequenceState = SEQ_PROMPT_TARGET;
        promptedTarget = false;
        Serial.println("INFO:Maintenance run complete.");
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
          
          for (int i = 0; i < MAX_PUMP_COUNT; ++i) {
            maintenancePumpActive[i] = false;
            maintenanceSpeed[i] = 0.0f;
            maintenancePumpEndTime[i] = 0;
          }
          
          unsigned long now = millis();
          bool anyPump = false;

          for (int i = 0; i < PUMP_COUNT; ++i) {
            if ((bitmask & (1 << i)) != 0 && pumpStepPins[i] != -1) {
              int pumpSeconds = seconds;
              if (pumpSeconds <= 0) {
                pumpSeconds = getPrimeDurationSec(pumpViscosity[i]);
              }

              maintenancePumpActive[i] = true;
              maintenancePumpEndTime[i] = now + ((unsigned long)pumpSeconds * 1000UL);
              anyPump = true;

              float pumpSpeed = speed;
              if (pumpSpeed <= 0.0f) {
                pumpSpeed = (pumpViscosity[i] >= 100.0f) ? 6000.0f : 12000.0f;
              }
              // Speed is in microsteps/s at 1/16 microstepping.
              maintenanceSpeed[i] = pumpSpeed * dir;
            }
          }

          if (!anyPump) {
            Serial.println("ERROR: No valid pumps in prime mask.");
            break;
          }
          
          activeMicrosteps = 16;
          setMicrostepping(activeMicrosteps);
          
          digitalWrite(SHARED_EN, LOW);
          delayMicroseconds(5);
          
          sequenceState = SEQ_MAINTENANCE_RUN;
          
          Serial.print("Starting parallel Prime/Purge for mask ");
          Serial.print(bitmask);
          Serial.print(" (dir=");
          Serial.print(dir);
          Serial.println(") with per-pump durations...");
          for (int i = 0; i < PUMP_COUNT; ++i) {
            if ((bitmask & (1 << i)) != 0 && pumpStepPins[i] != -1) {
              Serial.print("  Pump ");
              Serial.print(i + 1);
              Serial.print(": ");
              Serial.print((maintenancePumpEndTime[i] - now) / 1000UL);
              Serial.println("s");
            }
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
          
          for (int i = 0; i < MAX_PUMP_COUNT; ++i) {
            maintenancePumpActive[i] = false;
            maintenanceSpeed[i] = 0.0f;
            maintenancePumpEndTime[i] = 0;
          }
          
          int maxViscIndex = -1;
          float maxVisc = 0.0f;
          
          for (int i = 0; i < PUMP_COUNT; ++i) {
            if ((bitmask & (1 << i)) != 0 && pumpStepPins[i] != -1) {
              maintenancePumpActive[i] = true;
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
          
          unsigned long flushEndTime = millis() + ((unsigned long)seconds * 1000UL);

          for (int i = 0; i < PUMP_COUNT; ++i) {
            if (maintenancePumpActive[i]) {
              maintenancePumpEndTime[i] = flushEndTime;
              float pumpSpeed = speed;
              if (pumpSpeed <= 0.0f) {
                pumpSpeed = (pumpViscosity[i] >= 100.0f) ? 7000.0f : 14000.0f;
              }
              maintenanceSpeed[i] = pumpSpeed; // microsteps/s at 1/16 microstepping
            }
          }
          
          activeMicrosteps = 16;
          setMicrostepping(activeMicrosteps);
          
          digitalWrite(SHARED_EN, LOW);
          delayMicroseconds(5);
          
          sequenceState = SEQ_MAINTENANCE_RUN;
          
          Serial.print("Starting parallel Flush for mask ");
          Serial.print(bitmask);
          Serial.print(" for ");
          Serial.print(seconds);
          Serial.println("s...");
        }
        else if (strEqualsIgnoreCase(usbBuffer, "T")) {
          tareOffset = rawWeight;
          currentWeight = 0.0f;
          Serial.println("Scale Software Tared.");
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
          activeMicrosteps = 16;
          setMicrostepping(activeMicrosteps);

          AccelStepper& activePump = getPump(calibratingPumpIndex);
          activePump.setCurrentPosition(0);
          activePump.moveTo(CALIBRATION_RUN_STEPS);
          activePump.setSpeed(CALIBRATE_SPEED);

          sequenceState = SEQ_CALIBRATE_RUN;
          Serial.print("\n-> Starting calibration run for Pump ");
          Serial.println(calibratingPumpIndex + 1);
          Serial.println("-> Dispensing exactly 10000 microsteps (625 full steps) at 1/16 step...");
        }
        else if (sequenceState == SEQ_CALIBRATE_WAIT_INPUT) {
          float measuredWeight = strtof(usbBuffer, nullptr);
          if (measuredWeight > 0.02f) {
            float fullStepsTaken = 10000.0f / 16.0f;
            float calculatedSteps = fullStepsTaken / measuredWeight;

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
            float finalTarget = isZero ? 0.0f : target;
            int pumpIndex = targetEntryPumpIndex;
            targetWeights[pumpIndex] = finalTarget;

            if (finalTarget <= 0.0f) {
              Serial.print("Pump ");
              Serial.print(pumpIndex + 1);
              Serial.println(" Target set to: 0.00g (Skip)");
            } else {
              Serial.print("Pump ");
              Serial.print(pumpIndex + 1);
              Serial.print(" Target set to: ");
              Serial.print(finalTarget, 2);
              Serial.println("g");
            }

            if (targetEntryPumpIndex < PUMP_COUNT - 1) {
              targetEntryPumpIndex++;
              promptedTarget = false;
            } else {
              startFirstActivePump();
            }
          } else if (sequenceState == SEQ_PROMPT_TARGET) {
            Serial.print("ERROR: Invalid target. Must be > 1.50g (or 0.00g to skip).");
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
