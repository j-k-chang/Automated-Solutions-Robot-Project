#include "Multipump_dispensing.h"

namespace MultiPump {

// --- Dispensing Configuration ---
const float BULK_SPEED = 10000.0f;
const float TRICKLE_SPEED = 12000.0f;
const float CALIBRATE_SPEED = 2000.0f;
const long retractStepsWater = 3200;
const long retractStepsGlycerol = 9600;
const float TARGET_MARGIN = 0.03f;

// --- Global State ---
AccelStepper pump1(AccelStepper::DRIVER, PUMP1_STEP, SHARED_DIR);
AccelStepper pump2(AccelStepper::DRIVER, PUMP2_STEP, SHARED_DIR);

float currentWeight = 0.0f;
float rawWeight = 0.0f;
float tareOffset = 0.0f;
unsigned long lastScaleUpdateTime = 0;
unsigned long settleTimer = 0;
unsigned long stopTime = 0;
float lastSettleWeight = 0.0f;
bool newScaleData = false;

String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

// Sequence & Dispense State
SequenceState sequenceState = SEQ_PROMPT_PUMP1;
DispenseState dispenseState = DISPENSE_IDLE;

float targetPump1 = 0.0f;
float targetPump2 = 0.0f;
float startWeight = 0.0f;

// Calibration parameters per pump
float stepsPerGramLowPump1 = 700.0f;
float stepsPerGramHighPump1 = 1400.0f;
float stepsPerGramLowPump2 = 700.0f;
float stepsPerGramHighPump2 = 1400.0f;

// Viscosity settings per pump
bool isHighViscosityPump1 = false;
bool isHighViscosityPump2 = false;

int activeMicrosteps = 64;
long bulkEndSteps = 0;
long trimStepsRemaining = 0;
int calibratingPump = 1;
unsigned long settleBetweenTimer = 0;
bool promptedPump1 = false;
bool promptedPump2 = false;
unsigned long bulkStartTime = 0;

/**
 * @brief Parses incoming raw data from the scale and updates the current weight.
 */
void processScaleData(const String& raw);

/**
 * @brief Configures the microstepping resolution for TMC2209.
 */
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
    // Fallback to 1/16
    digitalWrite(SHARED_MS1, HIGH);
    digitalWrite(SHARED_MS2, LOW);
  }
}

/**
 * @brief Reads and processes commands sent from the host PC via USB Serial.
 */
void handleUsbCommands();

/**
 * @brief Adaptive variance-based settling slope detector.
 */
bool isScaleSettled(bool activePumpIsHighViscosity) {
  unsigned long settleGuardTime = activePumpIsHighViscosity ? 1500 : 1200;

  // 1. Enforce guard window
  if (millis() - stopTime < settleGuardTime) {
    return false; 
  }

  // 2. Only process when a new scale reading is available
  if (!newScaleData) {
    return false;
  }
  newScaleData = false; // Reset the flag

  // 3. Check for stabilization (no variance registered over 500ms)
  if (millis() - settleTimer > 500) { 
    if (abs(currentWeight - lastSettleWeight) < 0.005f) { 
      return true;
    }
    lastSettleWeight = currentWeight;
    settleTimer = millis();
  }
  return false;
}

void setupBulkFillHelper(AccelStepper& pump, float targetG, bool isHighVisc, float stepsPerGram) {
  startWeight = currentWeight;
  lastScaleUpdateTime = millis();
  
  float adjustedTarget = targetG - TARGET_MARGIN;
  float bulkTargetG = adjustedTarget * 0.85f;
  
  activeMicrosteps = 8; // 1/8 microstepping for bulk fill
  setMicrostepping(activeMicrosteps);
  
  bulkEndSteps = (bulkTargetG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(BULK_SPEED);
  
  digitalWrite(SHARED_EN, LOW); // Enable driver
  bulkStartTime = millis();
  dispenseState = DISPENSE_BULK_FILL;
}

void prepareTrimPulseHelper(AccelStepper& pump, float remaining, bool isHighVisc, float stepsPerGram) {
  dispenseState = DISPENSE_TRIM_PULSE;
  activeMicrosteps = isHighVisc ? 16 : 64;
  setMicrostepping(activeMicrosteps);
  
  float coefficient;
  if (remaining > 0.50f) {
    coefficient = 0.80f;
  } else if (remaining > 0.15f) {
    coefficient = isHighVisc ? 0.50f : 0.60f;
  } else {
    coefficient = isHighVisc ? 0.30f : 0.40f;
  }
  
  float pulseG = max(0.01f, remaining * coefficient);
  pulseG = min(pulseG, remaining);
  
  trimStepsRemaining = (pulseG * stepsPerGram) * activeMicrosteps;
  pump.setCurrentPosition(0);
  pump.setSpeed(TRICKLE_SPEED);
  
  Serial.print("-> Micro-Pulse: +");
  Serial.print(pulseG, 2);
  Serial.println("g");
}

/**
 * @brief Core state machine executing the dispensing logic for a single pump.
 */
bool dispensePump(AccelStepper& pump, float targetVal, bool isHighVisc, float stepsPerGram, long retractSteps, float retractSpeed) {
  float delivered = currentWeight - startWeight;
  float remaining = targetVal - delivered;

  switch (dispenseState) {
    case DISPENSE_IDLE:
      break;

    case DISPENSE_BULK_FILL:
      if (delivered >= targetVal) {
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
        
        if (remaining <= TARGET_MARGIN) {
          Serial.print("-> Remaining weight <= ");
          Serial.print(TARGET_MARGIN, 2);
          Serial.println("g. Halting dispense.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else if (delivered < targetVal) {
          prepareTrimPulseHelper(pump, remaining, isHighVisc, stepsPerGram);
        } else {
          dispenseState = DISPENSE_SUCK_BACK;
        }
      }
      break;

    case DISPENSE_TRIM_PULSE:
      if (delivered >= targetVal) {
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
        if (remaining <= TARGET_MARGIN) {
          Serial.print("-> Remaining weight <= ");
          Serial.print(TARGET_MARGIN, 2);
          Serial.println("g. Halting dispense.");
          dispenseState = DISPENSE_SUCK_BACK;
        } else if (delivered < targetVal) {
          prepareTrimPulseHelper(pump, remaining, isHighVisc, stepsPerGram);
        } else {
          dispenseState = DISPENSE_SUCK_BACK;
        }
      }
      break;

    case DISPENSE_SUCK_BACK:
      pump.setCurrentPosition(0);
      pump.setSpeed(retractSpeed);
      dispenseState = DISPENSE_RETRACTING;
      if (isHighVisc) {
        Serial.println("-> High Viscosity: Performing rapid pressure relief...");
      } else {
        Serial.println("-> Standard Retraction...");
      }
      break;

    case DISPENSE_RETRACTING:
      if (abs(pump.currentPosition()) >= retractSteps) {
        pump.stop();
        digitalWrite(SHARED_EN, HIGH); // Disable motors to prevent heating
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

  // Initialize shared control pins
  pinMode(SHARED_DIR, OUTPUT);
  pinMode(SHARED_EN, OUTPUT);
  pinMode(SHARED_MS1, OUTPUT);
  pinMode(SHARED_MS2, OUTPUT);

  // Initialize pump step pins
  pinMode(PUMP1_STEP, OUTPUT);
  pinMode(PUMP2_STEP, OUTPUT);

  // Disable both stepper drivers at startup
  digitalWrite(SHARED_EN, HIGH);
  
  // Set default microstepping
  setMicrostepping(16);

  // Configure kinematics limits
  pump1.setMaxSpeed(16000);
  pump1.setAcceleration(8000);
  pump2.setMaxSpeed(16000);
  pump2.setAcceleration(8000);

  Serial.println("\n========================================");
  Serial.println("Multi-Pump Gravimetric Dispensing System");
  Serial.println("Parallel Wiring: Shared EN, DIR, MS1/MS2");
  Serial.println("Pump 1 Step: pin 2 | Pump 2 Step: pin 7");
  Serial.println("========================================");
  Serial.println("Send 'H1'/'L1' to toggle Pump 1 Glycerol/Water");
  Serial.println("Send 'H2'/'L2' to toggle Pump 2 Glycerol/Water");
  Serial.println("Send 'C1' or 'C2' to calibrate active profile.");
  Serial.println("Enter target weight > 1.50 to start.");
  Serial.println("========================================");
}

void multipumpLoop() {
  // Always read incoming scale data
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '+' || c == '-') {
      if (scaleBuffer.length() > 0) processScaleData(scaleBuffer);
      scaleBuffer = String(c);
    } else if (scaleBuffer.length() < MAX_BUF) {
      scaleBuffer += c;
    }
  }

  // Handle USB commands
  handleUsbCommands();

  // Watchdog Timer: Stop pump if scale drops offline
  if (sequenceState == SEQ_DISPENSE_PUMP1 || sequenceState == SEQ_DISPENSE_PUMP2) {
    if (dispenseState == DISPENSE_BULK_FILL || dispenseState == DISPENSE_TRIM_PULSE || dispenseState == DISPENSE_RETRACTING) {
      if (millis() - lastScaleUpdateTime > 1000) {
        pump1.stop();
        pump2.stop();
        digitalWrite(SHARED_EN, HIGH); // Disable both drivers
        dispenseState = DISPENSE_ERROR;
        sequenceState = SEQ_PROMPT_PUMP1;
        promptedPump1 = false;
        promptedPump2 = false;
        targetPump1 = 0.0f;
        targetPump2 = 0.0f;
        Serial.println("\n!!! ERROR: SCALE TIMEOUT WATCHDOG !!!");
      }
    }
  }

  // Manage sequence
  switch (sequenceState) {
    case SEQ_PROMPT_PUMP1: {
      if (!promptedPump1) {
        Serial.print("Pump 1 Viscosity: ");
        Serial.println(isHighViscosityPump1 ? "HIGH (Glycerol)" : "LOW (Water)");
        Serial.println("Enter weight (g) for Pump 1:");
        promptedPump1 = true;
      }
      break;
    }

    case SEQ_DISPENSE_PUMP1: {
      bool isHighVisc = isHighViscosityPump1;
      float stepsPerGram = isHighVisc ? stepsPerGramHighPump1 : stepsPerGramLowPump1;
      long retractSteps = isHighVisc ? retractStepsGlycerol : retractStepsWater;
      float retractSpeed = isHighVisc ? -16000.0f : -1200.0f;
      float adjustedTarget = targetPump1 - TARGET_MARGIN;

      if (dispensePump(pump1, adjustedTarget, isHighVisc, stepsPerGram, retractSteps, retractSpeed)) {
        Serial.print("Pump 1 done. Dispensed: ");
        Serial.println(currentWeight - startWeight, 2);
        dispenseState = DISPENSE_IDLE;
        sequenceState = SEQ_SETTLE_BETWEEN;
        settleBetweenTimer = millis();
        Serial.println("\n-> Waiting 5 seconds for scale to settle...");
      }
      break;
    }

    case SEQ_SETTLE_BETWEEN: {
      if (millis() - settleBetweenTimer >= 5000) {
        if (targetPump2 <= 0.0f) {
          Serial.println("-> Scale settled. Pump 2 is set to 0.00g (Skip). Dispensing complete.");
          sequenceState = SEQ_DONE;
        } else {
          Serial.println("-> Scale settled. Preparing Pump 2...");
          
          // Prepare Pump 2 dispense using new settled weight
          bool isHighVisc = isHighViscosityPump2;
          float stepsPerGram = isHighVisc ? stepsPerGramHighPump2 : stepsPerGramLowPump2;
          setupBulkFillHelper(pump2, targetPump2, isHighVisc, stepsPerGram);
          sequenceState = SEQ_DISPENSE_PUMP2;
          
          Serial.print("\nStarting Pump 2 Dispense. Target: ");
          Serial.print(targetPump2, 2);
          Serial.print("g (Adjusted target: ");
          Serial.print(targetPump2 - TARGET_MARGIN, 2);
          Serial.println("g)");
          Serial.print("-> Aiming for ");
          Serial.print((targetPump2 - TARGET_MARGIN) * 0.85f, 2);
          Serial.print("g bulk fill (");
          Serial.print(bulkEndSteps);
          Serial.println(" steps at 1/8 microstep)");
        }
      }
      break;
    }

    case SEQ_PROMPT_PUMP2: {
      if (!promptedPump2) {
        Serial.print("Pump 2 Viscosity: ");
        Serial.println(isHighViscosityPump2 ? "HIGH (Glycerol)" : "LOW (Water)");
        Serial.println("Enter weight (g) for Pump 2:");
        promptedPump2 = true;
      }
      break;
    }

    case SEQ_DISPENSE_PUMP2: {
      bool isHighVisc = isHighViscosityPump2;
      float stepsPerGram = isHighVisc ? stepsPerGramHighPump2 : stepsPerGramLowPump2;
      long retractSteps = isHighVisc ? retractStepsGlycerol : retractStepsWater;
      float retractSpeed = isHighVisc ? -16000.0f : -1200.0f;
      float adjustedTarget = targetPump2 - TARGET_MARGIN;

      if (dispensePump(pump2, adjustedTarget, isHighVisc, stepsPerGram, retractSteps, retractSpeed)) {
        Serial.print("Pump 2 done. Dispensed: ");
        Serial.println(currentWeight - startWeight, 2);
        dispenseState = DISPENSE_IDLE;
        sequenceState = SEQ_DONE;
      }
      break;
    }

    case SEQ_DONE:
      Serial.println("\nEntire multi-pump dispensing sequence completed successfully!");
      Serial.println("========================================\n");
      targetPump1 = 0.0f;
      targetPump2 = 0.0f;
      sequenceState = SEQ_PROMPT_PUMP1;
      promptedPump1 = false;
      promptedPump2 = false;
      break;

    case SEQ_CALIBRATE_RUN: {
      AccelStepper& activePump = (calibratingPump == 1) ? pump1 : pump2;
      if (activePump.distanceToGo() == 0) {
        activePump.stop();
        digitalWrite(SHARED_EN, HIGH); // Disable motor driver holding current
        
        Serial.println("\n-> Calibration run complete (10000 steps).");
        Serial.println("-> Please weigh the dispensed liquid on your scale.");
        Serial.println("-> Enter the measured weight in grams (e.g. 1.29) below:");
        
        sequenceState = SEQ_CALIBRATE_WAIT_INPUT;
        usbBuffer = "";
      } else {
        activePump.runSpeedToPosition();
      }
      break;
    }

    case SEQ_CALIBRATE_WAIT_INPUT:
      // Handled inside handleUsbCommands
      break;
  }
}

void processScaleData(const String& raw) {
  String cleanStr = "";
  for (unsigned int i = 0; i < raw.length(); i++) {
    char ch = raw.charAt(i);
    if (isDigit(ch) || ch == '.' || ch == '-') cleanStr += ch;
  }
  if (cleanStr.length() > 0) {
    rawWeight = cleanStr.toFloat();
    currentWeight = rawWeight - tareOffset;
    lastScaleUpdateTime = millis();
    newScaleData = true; // Signal that new weight data has arrived

    // Print real-time telemetry over USB Serial (9600 Baud)
    bool activeIsHighViscosity = false;
    if (sequenceState == SEQ_DISPENSE_PUMP1 || sequenceState == SEQ_PROMPT_PUMP1) {
      activeIsHighViscosity = isHighViscosityPump1;
    } else if (sequenceState == SEQ_DISPENSE_PUMP2 || sequenceState == SEQ_PROMPT_PUMP2) {
      activeIsHighViscosity = isHighViscosityPump2;
    }

    Serial.print("TELEMETRY:");
    Serial.print(currentWeight, 2);
    Serial.print(",");
    Serial.print((int)dispenseState);
    Serial.print(",");
    Serial.println(activeIsHighViscosity ? "1" : "0");
  }
}

void handleUsbCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (usbBuffer.length() > 0) {
        usbBuffer.trim();
        
        // --- Emergency Stop Command ---
        if (usbBuffer.equalsIgnoreCase("S")) {
          pump1.stop();
          pump2.stop();
          digitalWrite(SHARED_EN, HIGH); 
          dispenseState = DISPENSE_IDLE;
          sequenceState = SEQ_PROMPT_PUMP1;
          promptedPump1 = false;
          promptedPump2 = false;
          Serial.println("!!! EMERGENCY PUMP HALT !!!");
        } 
        
        // --- Tare Command ---
        else if (usbBuffer.equalsIgnoreCase("T")) {
          tareOffset = rawWeight;
          currentWeight = 0.0f;
          Serial.println("Scale Software Tared.");
        }
        
        // --- Viscosity Mode: Pump 1 ---
        else if (usbBuffer.equalsIgnoreCase("H1")) {
          isHighViscosityPump1 = true;
          Serial.println("Pump 1 Viscosity Mode: HIGH (Glycerol)");
        }
        else if (usbBuffer.equalsIgnoreCase("L1")) {
          isHighViscosityPump1 = false;
          Serial.println("Pump 1 Viscosity Mode: LOW (Water)");
        }
        
        // --- Viscosity Mode: Pump 2 ---
        else if (usbBuffer.equalsIgnoreCase("H2")) {
          isHighViscosityPump2 = true;
          Serial.println("Pump 2 Viscosity Mode: HIGH (Glycerol)");
        }
        else if (usbBuffer.equalsIgnoreCase("L2")) {
          isHighViscosityPump2 = false;
          Serial.println("Pump 2 Viscosity Mode: LOW (Water)");
        }
        
        // --- Calibration Mode Activation ---
        else if (usbBuffer.equalsIgnoreCase("C1") || usbBuffer.equalsIgnoreCase("C2") || 
                 (usbBuffer.equalsIgnoreCase("C") && (sequenceState == SEQ_PROMPT_PUMP1 || sequenceState == SEQ_PROMPT_PUMP2))) {
          if (sequenceState == SEQ_PROMPT_PUMP1 || sequenceState == SEQ_PROMPT_PUMP2) {
            if (usbBuffer.equalsIgnoreCase("C2") || (usbBuffer.equalsIgnoreCase("C") && sequenceState == SEQ_PROMPT_PUMP2)) {
              calibratingPump = 2;
            } else {
              calibratingPump = 1;
            }

            digitalWrite(SHARED_EN, LOW); // Enable motor driver
            activeMicrosteps = 16;        // 1/16 microstepping for calibration
            setMicrostepping(activeMicrosteps);
            
            AccelStepper& activePump = (calibratingPump == 1) ? pump1 : pump2;
            activePump.setCurrentPosition(0);
            activePump.moveTo(10000);     // Dispense exactly 10000 microsteps
            activePump.setSpeed(CALIBRATE_SPEED);
            
            sequenceState = SEQ_CALIBRATE_RUN;
            Serial.print("\n-> Starting calibration run for Pump ");
            Serial.println(calibratingPump);
            Serial.println("-> Dispensing exactly 10000 microsteps (625 full steps) at 1/16 step...");
          } else {
            Serial.println("Error: Can only calibrate when pump is in IDLE/PROMPT state.");
          }
        }
        
        // --- Calibration Input State ---
        else if (sequenceState == SEQ_CALIBRATE_WAIT_INPUT) {
          float measuredWeight = usbBuffer.toFloat();
          if (measuredWeight > 0.02f) {
            // 10000 steps were taken at 1/16 microstepping = 625 Full Steps
            float fullStepsTaken = 10000.0f / 16.0f;
            float calculatedSteps = fullStepsTaken / measuredWeight;
            
            if (calibratingPump == 1) {
              if (isHighViscosityPump1) {
                stepsPerGramHighPump1 = calculatedSteps;
              } else {
                stepsPerGramLowPump1 = calculatedSteps;
              }
            } else {
              if (isHighViscosityPump2) {
                stepsPerGramHighPump2 = calculatedSteps;
              } else {
                stepsPerGramLowPump2 = calculatedSteps;
              }
            }
            
            Serial.println("\n========================================");
            Serial.println("Calibration Successful!");
            Serial.print("Pump "); Serial.print(calibratingPump);
            Serial.print(" Measured weight: "); Serial.print(measuredWeight, 2); Serial.println("g");
            Serial.print("New steps/gram [");
            if (calibratingPump == 1) {
              Serial.print(isHighViscosityPump1 ? "Glycerol" : "Water");
            } else {
              Serial.print(isHighViscosityPump2 ? "Glycerol" : "Water");
            }
            Serial.print("]: ");
            Serial.println(calculatedSteps, 2);
            Serial.println("========================================");
            
            sequenceState = (calibratingPump == 1) ? SEQ_PROMPT_PUMP1 : SEQ_PROMPT_PUMP2;
          } else {
            Serial.println("Error: Invalid weight entered. Please enter a valid weight > 0.02g:");
          }
        }
        
        // --- Start standard Dispense Target ---
        else {
          float target = usbBuffer.toFloat();
          // Also check if the buffer is exactly "0" or "0.0"
          bool isZero = usbBuffer.equals("0") || usbBuffer.equals("0.0") || usbBuffer.equals("0.00");
          
          if (isZero || target > 1.5f) {
            float finalTarget = isZero ? 0.0f : target;
            
            if (sequenceState == SEQ_PROMPT_PUMP1) {
              targetPump1 = finalTarget;
              if (targetPump1 <= 0.0f) {
                Serial.println("Pump 1 Target set to: 0.00g (Skip)");
              } else {
                Serial.print("Pump 1 Target set to: ");
                Serial.print(targetPump1, 2);
                Serial.println("g");
              }
              
              sequenceState = SEQ_PROMPT_PUMP2;
              promptedPump2 = false; // Trigger prompt for Pump 2
            } 
            else if (sequenceState == SEQ_PROMPT_PUMP2) {
              targetPump2 = finalTarget;
              if (targetPump2 <= 0.0f) {
                Serial.println("Pump 2 Target set to: 0.00g (Skip)\n");
              } else {
                Serial.print("Pump 2 Target set to: ");
                Serial.print(targetPump2, 2);
                Serial.println("g\n");
              }
              
              // Direct sequence routing based on target combinations
              if (targetPump1 <= 0.0f && targetPump2 <= 0.0f) {
                Serial.println("Both targets set to 0.00g. Dispensing skipped.");
                sequenceState = SEQ_DONE;
              }
              else if (targetPump1 <= 0.0f) {
                // Pump 1 skipped, go straight to preparing Pump 2
                float stepsPerGram = isHighViscosityPump2 ? stepsPerGramHighPump2 : stepsPerGramLowPump2;
                setupBulkFillHelper(pump2, targetPump2, isHighViscosityPump2, stepsPerGram);
                sequenceState = SEQ_DISPENSE_PUMP2;
                
                Serial.println("Starting Sequential Dispense.");
                Serial.println("Pump 1 done. Dispensed: 0.00g");
                Serial.println("-> Scale settled. Preparing Pump 2...");
                Serial.print("Starting Pump 2 Dispense. Target: ");
                Serial.print(targetPump2, 2);
                Serial.print("g (Adjusted target: ");
                Serial.print(targetPump2 - TARGET_MARGIN, 2);
                Serial.println("g)");
                Serial.print("-> Aiming for ");
                Serial.print((targetPump2 - TARGET_MARGIN) * 0.85f, 2);
                Serial.print("g bulk fill (");
                Serial.print(bulkEndSteps);
                Serial.println(" steps at 1/8 microstep)");
              }
              else {
                // Standard setup: start Pump 1 dispensing
                float stepsPerGram = isHighViscosityPump1 ? stepsPerGramHighPump1 : stepsPerGramLowPump1;
                setupBulkFillHelper(pump1, targetPump1, isHighViscosityPump1, stepsPerGram);
                sequenceState = SEQ_DISPENSE_PUMP1;
                
                Serial.print("Starting Sequential Dispense.\n-> Pump 1 Target: ");
                Serial.print(targetPump1, 2);
                Serial.print("g (Adjusted target: ");
                Serial.print(targetPump1 - TARGET_MARGIN, 2);
                Serial.println("g)");
                Serial.print("-> Aiming for ");
                Serial.print((targetPump1 - TARGET_MARGIN) * 0.85f, 2);
                Serial.print("g bulk fill (");
                Serial.print(bulkEndSteps);
                Serial.println(" steps at 1/8 microstep)");
              }
            }
          } else {
            Serial.println("Error: Enter target weight > 1.50g (or 0.0g to skip), 'H1'/'L1' for Pump 1, 'H2'/'L2' for Pump 2, or 'C1'/'C2' to calibrate.");
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
