#include "Multipump_dispensing.h"

namespace MultiPump {

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

AccelStepper* const pumps[PUMP_COUNT] = {
  &pump1, &pump2, &pump3, &pump4
};

const int pumpStepPins[PUMP_COUNT] = {
  PUMP1_STEP, PUMP2_STEP, PUMP3_STEP, PUMP4_STEP
};

void processScaleData(const String& raw);
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

String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

SequenceState sequenceState = SEQ_PROMPT_TARGET;
DispenseState dispenseState = DISPENSE_IDLE;

int targetEntryPumpIndex = 0;
int activePumpIndex = -1;
int calibratingPumpIndex = 0;
int nextPumpIndex = -1;
bool promptedTarget = false;
bool completionAnnounced = false;

float targetWeights[PUMP_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};

float stepsPerGramLow[PUMP_COUNT] = {700.0f, 700.0f, 700.0f, 700.0f};
float stepsPerGramHigh[PUMP_COUNT] = {1400.0f, 1400.0f, 1400.0f, 1400.0f};

bool highViscosity[PUMP_COUNT] = {false, false, false, false};

int activeMicrosteps = 64;
long bulkEndSteps = 0;
long trimStepsRemaining = 0;
unsigned long settleBetweenTimer = 0;
unsigned long bulkStartTime = 0;

AccelStepper& getPump(int pumpIndex) {
  return *pumps[pumpIndex];
}

bool isValidPumpIndex(int pumpIndex) {
  return pumpIndex >= 0 && pumpIndex < PUMP_COUNT;
}

float getStepsPerGram(int pumpIndex) {
  return highViscosity[pumpIndex] ? stepsPerGramHigh[pumpIndex] : stepsPerGramLow[pumpIndex];
}

long getRetractSteps(int pumpIndex) {
  return highViscosity[pumpIndex] ? retractStepsGlycerol : retractStepsWater;
}

float getRetractSpeed(int pumpIndex) {
  return highViscosity[pumpIndex] ? -16000.0f : -1200.0f;
}

float getStopLeadG(int pumpIndex) {
  return highViscosity[pumpIndex] ? HIGH_VISC_STOP_LEAD_G : LOW_VISC_STOP_LEAD_G;
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
}

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

bool isScaleSettled(bool activePumpIsHighViscosity) {
  unsigned long settleGuardTime = activePumpIsHighViscosity ? HIGH_VISC_SETTLE_GUARD_MS : LOW_VISC_SETTLE_GUARD_MS;

  if (millis() - stopTime < settleGuardTime) {
    return false;
  }

  if (!newScaleData) {
    return false;
  }
  newScaleData = false;

  if (millis() - settleTimer > 500) {
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
  float bulkTargetG = adjustedTarget * 0.85f;
  float stepsPerGram = getStepsPerGram(pumpIndex);

  activeMicrosteps = 8;
  setMicrostepping(activeMicrosteps);

  bulkEndSteps = (bulkTargetG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(BULK_SPEED);

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

  float coefficient;
  if (remaining > 0.50f) {
    coefficient = 0.80f;
  } else if (remaining > 0.15f) {
    coefficient = isHighVisc ? 0.50f : 0.60f;
  } else {
    coefficient = isHighVisc ? 0.15f : 0.40f;
  }

  float minPulseG = isHighVisc ? 0.005f : 0.01f;
  float pulseG = max(minPulseG, remaining * coefficient);
  pulseG = min(pulseG, remaining);

  trimStepsRemaining = (pulseG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(TRICKLE_SPEED);

  Serial.print("-> Micro-Pulse: +");
  Serial.print(pulseG, 2);
  Serial.println("g");
}

void startPumpDispense(int pumpIndex) {
  float targetG = targetWeights[pumpIndex];
  float adjustedTarget = targetG - getStopLeadG(pumpIndex);

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
  Serial.print(adjustedTarget * 0.85f, 2);
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
        float currentTargetSpeed = BULK_SPEED;
        const unsigned long RAMP_TIME_MS = 400;
        const float START_SPEED = 1500.0f;

        if (elapsed < RAMP_TIME_MS) {
          currentTargetSpeed = START_SPEED + ((BULK_SPEED - START_SPEED) * ((float)elapsed / RAMP_TIME_MS));
        }
        pump.setSpeed(currentTargetSpeed);
        pump.runSpeed();
      }
      break;

    case DISPENSE_SETTLE_BULK:
      if (isScaleSettled(isHighVisc)) {
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
      if (isScaleSettled(isHighVisc)) {
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
      dispenseState = DISPENSE_IDLE;
      return true;

    default:
      break;
  }

  return false;
}

void multipumpSetup() {
  delay(2000);

  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD);

  pinMode(SHARED_DIR, OUTPUT);
  pinMode(SHARED_EN, OUTPUT);
  pinMode(SHARED_MS1, OUTPUT);
  pinMode(SHARED_MS2, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // Start with fan off

  for (int index = 0; index < PUMP_COUNT; ++index) {
    pinMode(pumpStepPins[index], OUTPUT);
    pumps[index]->setMaxSpeed(16000);
    pumps[index]->setAcceleration(8000);
  }

  digitalWrite(SHARED_EN, HIGH);
  setMicrostepping(16);

  Serial.println("\n========================================");
  Serial.println("Multi-Pump Gravimetric Dispensing System");
  Serial.println("Parallel Wiring: Shared EN, DIR, MS1/MS2");
  Serial.println("Pump 1 Step: pin 2 | Pump 2 Step: pin 7 | Pump 3 Step: pin 8 | Pump 4 Step: pin 9");
  Serial.println("========================================");
  Serial.println("Send 'H1'/'L1' through 'H4'/'L4' to toggle Pump 1-4 Glycerol/Water");
  Serial.println("Send 'C1' through 'C4' to calibrate an active profile.");
  Serial.println("Send 'FAN ON' or 'FAN OFF' to turn the DC fan (Pin 22) on/off.");
  Serial.println("Enter target weight > 1.50 for each pump, or 0.0 to skip.");
  Serial.println("========================================");

  resetRunState();
}

void multipumpLoop() {
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '+' || c == '-') {
      if (scaleBuffer.length() > 0) {
        processScaleData(scaleBuffer);
      }
      scaleBuffer = String(c);
    } else if (scaleBuffer.length() < MAX_BUF) {
      scaleBuffer += c;
    }
  }

  handleUsbCommands();

  if (sequenceState == SEQ_DISPENSE_ACTIVE) {
    if (dispenseState == DISPENSE_BULK_FILL ||
        dispenseState == DISPENSE_TRIM_PULSE ||
        dispenseState == DISPENSE_PRESSURE_RELIEF_FAST ||
        dispenseState == DISPENSE_PRESSURE_RELIEF_FINISH ||
        dispenseState == DISPENSE_RETRACTING) {
      if (millis() - lastScaleUpdateTime > 1000) {
        for (int index = 0; index < PUMP_COUNT; ++index) {
          pumps[index]->stop();
        }
        digitalWrite(SHARED_EN, HIGH);
        dispenseState = DISPENSE_ERROR;
        Serial.println("\n!!! ERROR: SCALE TIMEOUT WATCHDOG !!!");
        resetRunState();
      }
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
            sequenceState = SEQ_DONE;
          } else {
            sequenceState = SEQ_SETTLE_BETWEEN;
            settleBetweenTimer = millis();
            Serial.println("\n-> Waiting 5 seconds for scale to settle before the next pump...");
          }
        }
      }
      break;
    }

    case SEQ_SETTLE_BETWEEN:
      if (millis() - settleBetweenTimer >= 5000) {
        if (nextPumpIndex < 0) {
          sequenceState = SEQ_DONE;
        } else {
          activePumpIndex = nextPumpIndex;
          sequenceState = SEQ_DISPENSE_ACTIVE;
          Serial.print("-> Scale settled. Preparing Pump ");
          Serial.print(activePumpIndex + 1);
          Serial.println("...");
          startPumpDispense(activePumpIndex);
        }
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
          usbBuffer = "";
        } else {
          activePump.runSpeedToPosition();
        }
      }
      break;
    }

    case SEQ_CALIBRATE_WAIT_INPUT:
      break;
  }
}

void processScaleData(const String& raw) {
  String cleanStr = "";
  for (unsigned int index = 0; index < raw.length(); index++) {
    char ch = raw.charAt(index);
    if (isDigit(ch) || ch == '.' || ch == '-') {
      cleanStr += ch;
    }
  }

  if (cleanStr.length() > 0) {
    rawWeight = cleanStr.toFloat();
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
      if (usbBuffer.length() > 0) {
        usbBuffer.trim();

        if (usbBuffer.equalsIgnoreCase("S")) {
          for (int index = 0; index < PUMP_COUNT; ++index) {
            pumps[index]->stop();
          }
          digitalWrite(SHARED_EN, HIGH);
          Serial.println("!!! EMERGENCY PUMP HALT !!!");
          resetRunState();
        }
        else if (usbBuffer.equalsIgnoreCase("T")) {
          tareOffset = rawWeight;
          currentWeight = 0.0f;
          Serial.println("Scale Software Tared.");
        }
        else if (usbBuffer.equalsIgnoreCase("FAN ON") || usbBuffer.equalsIgnoreCase("FAN_ON") || usbBuffer.equalsIgnoreCase("FON")) {
          digitalWrite(FAN_PIN, HIGH);
          Serial.println("DC Fan: ON");
        }
        else if (usbBuffer.equalsIgnoreCase("FAN OFF") || usbBuffer.equalsIgnoreCase("FAN_OFF") || usbBuffer.equalsIgnoreCase("FOFF")) {
          digitalWrite(FAN_PIN, LOW);
          Serial.println("DC Fan: OFF");
        }
        else if (usbBuffer.length() == 2 &&
                 (usbBuffer.charAt(0) == 'H' || usbBuffer.charAt(0) == 'h' ||
                  usbBuffer.charAt(0) == 'L' || usbBuffer.charAt(0) == 'l') &&
                 isDigit(usbBuffer.charAt(1))) {
          int pumpNumber = usbBuffer.charAt(1) - '0';
          int pumpIndex = pumpNumber - 1;
          if (isValidPumpIndex(pumpIndex)) {
            highViscosity[pumpIndex] = (usbBuffer.charAt(0) == 'H' || usbBuffer.charAt(0) == 'h');
            Serial.print("Pump ");
            Serial.print(pumpNumber);
            Serial.print(" Viscosity Mode: ");
            Serial.println(highViscosity[pumpIndex] ? "HIGH (Glycerol)" : "LOW (Water)");
          } else {
            Serial.println("Error: Pump index must be 1 through 4.");
          }
        }
        else if (sequenceState == SEQ_PROMPT_TARGET &&
                 (usbBuffer.length() == 1 || usbBuffer.length() == 2) &&
                 (usbBuffer.charAt(0) == 'C' || usbBuffer.charAt(0) == 'c')) {
          if (usbBuffer.length() == 2 && isDigit(usbBuffer.charAt(1))) {
            int pumpNumber = usbBuffer.charAt(1) - '0';
            int pumpIndex = pumpNumber - 1;
            if (isValidPumpIndex(pumpIndex)) {
              calibratingPumpIndex = pumpIndex;
            } else {
              Serial.println("Error: Pump index must be 1 through 4.");
              usbBuffer = "";
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
          activePump.moveTo(10000);
          activePump.setSpeed(CALIBRATE_SPEED);

          sequenceState = SEQ_CALIBRATE_RUN;
          Serial.print("\n-> Starting calibration run for Pump ");
          Serial.println(calibratingPumpIndex + 1);
          Serial.println("-> Dispensing exactly 10000 microsteps (625 full steps) at 1/16 step...");
        }
        else if (sequenceState == SEQ_CALIBRATE_WAIT_INPUT) {
          float measuredWeight = usbBuffer.toFloat();
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
            Serial.println("Error: Invalid weight entered. Please enter a valid weight > 0.02g:");
          }
        }
        else {
          float target = usbBuffer.toFloat();
          bool isZero = usbBuffer.equals("0") || usbBuffer.equals("0.0") || usbBuffer.equals("0.00");

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
            Serial.println("Error: Enter target weight > 1.50g (or 0.0g to skip), 'H1' through 'H4'/'L1' through 'L4', or 'C1' through 'C4' to calibrate.");
          }
        }

        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) {
      usbBuffer += c;
    }
  }
}

} // namespace MultiPump
