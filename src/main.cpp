#include <Arduino.h>
#include <AccelStepper.h>

/**
 * =================================================================================
 * Gravimetric Dispensing System V6 (Adaptive Progressive Approximation - Strategy 1)
 * =================================================================================
 * High-precision, zero-overshoot dispensing featuring dynamically selectable
 * viscosity profiles (Water vs. Glycerol) and packet-aware scale settle detection.
 * 
 * Microstepping Settings (Strictly compliant with TMC2209 Standalone Mode):
 *  - 1/64 Microstep (MS1 = LOW, MS2 = HIGH) for ultra-high calibration and trim resolution (Water).
 *  - 1/8 Microstep (MS1 = LOW, MS2 = LOW) for high-speed volumetric bulk fill (Both).
 *  - 1/16 Microstep (MS1 = HIGH, MS2 = LOW) for high torque/speed trim (Glycerol) and calibration.
 */

// --- Physical Pin Out ---
const int stepPin = 2;
const int dirPin = 3;
const int enPin = 4;
const int ms1Pin = 5;
const int ms2Pin = 6;

// --- Communication Settings ---
const long SCALE_BAUD = 9600;
const long USB_BAUD = 9600; 

// --- Motor Speed Settings (Microsteps per second) ---
const float CALIBRATE_SPEED = 2000.0;  // 2000 microsteps/s at 1/16 for controlled calibration run
const float BULK_SPEED = 10000.0;      // 10000 microsteps/s at 1/8 for fast bulk fill
const float TRICKLE_SPEED = 12000.0;   // 12000 microsteps/s for fine drop-by-drop trim

// --- State Machine Definitions ---
enum ControlState {
  STATE_IDLE,
  STATE_CALIBRATE_RUN,
  STATE_CALIBRATE_WAIT_INPUT,
  STATE_BULK_FILL,
  STATE_SETTLE_BULK,
  STATE_TRIM_PULSE,
  STATE_SETTLE_TRIM,
  STATE_SUCK_BACK,
  STATE_RETRACTING,
  STATE_COMPLETE,
  STATE_ERROR_STATE
};

ControlState currentState = STATE_IDLE;

// --- Global Variables ---
AccelStepper pump(AccelStepper::DRIVER, stepPin, dirPin);

// Calibration profiles (Full Steps per Gram)
float stepsPerGramLow = 700.0;    // Water-like default calibration
float stepsPerGramHigh = 1400.0;  // Glycerol/Syrup default calibration

// Dynamic Viscosity Mode Toggled by User ('H' or 'L')
bool isHighViscosity = false; // Starts in standard Water mode (Low)

// Target safety margin to prevent any overshoot
const float TARGET_MARGIN = 0.04;

float targetWeight = 0.0;
float startWeight = 0.0;
float currentWeight = 0.0;
float lastSettleWeight = 0.0;

unsigned long lastScaleUpdateTime = 0;
unsigned long settleTimer = 0;
unsigned long stopTime = 0; // Timestamp of when the pump stopped

int activeMicrosteps = 64;   // Resolution tracking
long bulkEndSteps = 0;
long trimStepsRemaining = 0;

String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

bool newScaleData = false; // Set to true when a fresh scale packet is successfully parsed

// --- Functions Declarations ---
void processScaleData(String raw);
void handleUsbCommands();
void setMicrostepping(int resolution);
bool isScaleSettled();

void setup() {
  delay(2000); 
  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD); 

  pinMode(enPin, OUTPUT);
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  
  digitalWrite(enPin, HIGH); // Start disabled
  
  activeMicrosteps = isHighViscosity ? 16 : 64;
  setMicrostepping(activeMicrosteps); 

  pump.setMaxSpeed(16000);
  pump.setAcceleration(8000);

  Serial.println("\n========================================");
  Serial.println("Progressive Approximation Gravimetric Loop");
  Serial.println("Mode: Dynamically Selectable Viscosity Profiles");
  Serial.println("========================================");
  Serial.println("Send 'L' for Low Viscosity Mode (Water - Default)");
  Serial.println("Send 'H' for High Viscosity Mode (Glycerol)");
  Serial.println("Send 'C' to calibrate active profile.");
  Serial.println("Type target weight (e.g. '20.0') to start.");
  Serial.println("----------------------------------------");
}

void loop() {
  // Read scale serial buffer
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '+' || c == '-') {
      if (scaleBuffer.length() > 0) processScaleData(scaleBuffer);
      scaleBuffer = String(c); 
    } else if (scaleBuffer.length() < MAX_BUF) {
      scaleBuffer += c;
    }
  }

  handleUsbCommands();

  // Safety Watchdog: Stop pump if scale drops offline during an active dispense run
  if (currentState == STATE_BULK_FILL || currentState == STATE_TRIM_PULSE || currentState == STATE_RETRACTING) {
    if (millis() - lastScaleUpdateTime > 1000) {
      pump.stop();
      digitalWrite(enPin, HIGH); 
      currentState = STATE_ERROR_STATE;
      Serial.println("\n!!! ERROR: SCALE TIMEOUT WATCHDOG !!!");
    }
  }

  float delivered = currentWeight - startWeight;
  float remaining = targetWeight - delivered;

  switch (currentState) {
    case STATE_IDLE:
      // Handled in handleUsbCommands
      break;

    case STATE_CALIBRATE_RUN:
      if (pump.distanceToGo() == 0) {
        pump.stop();
        digitalWrite(enPin, HIGH); // Disable pump holding current
        
        Serial.println("\n-> Calibration run complete (10000 steps).");
        Serial.println("-> Please weigh the dispensed liquid on your scale.");
        Serial.println("-> Enter the measured weight in grams (e.g. 1.29) below:");
        
        currentState = STATE_CALIBRATE_WAIT_INPUT;
        usbBuffer = "";
      } else {
        pump.runSpeedToPosition();
      }
      break;

    case STATE_CALIBRATE_WAIT_INPUT:
      // Handled inside handleUsbCommands
      break;

    case STATE_BULK_FILL:
      if (pump.currentPosition() >= bulkEndSteps) {
        pump.stop();
        stopTime = millis(); // Enforce guard timer start
        
        activeMicrosteps = isHighViscosity ? 16 : 64; // 1/16 for glycerol, 1/64 for water
        setMicrostepping(activeMicrosteps);
        
        currentState = STATE_SETTLE_BULK;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
        Serial.println("-> Bulk Fill complete. Settling scale...");
      } else {
        pump.runSpeed();
      }
      break;

    case STATE_SETTLE_BULK:
      if (isScaleSettled()) {
        Serial.print("-> Bulk Settled at: "); 
        Serial.print(delivered, 2); 
        Serial.println("g");
        
        // Safety Cut-off: If remaining is under a single drop (0.04g), halt immediately
        if (remaining <= 0.04) {
          Serial.println("-> Remaining weight <= 0.04g (within 1 drop). Halting dispense.");
          currentState = STATE_SUCK_BACK;
        } else if (delivered < targetWeight) {
          currentState = STATE_TRIM_PULSE;
          activeMicrosteps = isHighViscosity ? 16 : 64;
          setMicrostepping(activeMicrosteps);
          
          float stepsPerGram = isHighViscosity ? stepsPerGramHigh : stepsPerGramLow;
          
          // Calculate safe tiny pulse: 50% of remaining weight, capped, min 0.04g
          float pulseG = max(0.04f, remaining * 0.5f);
          pulseG = min(pulseG, remaining);
          
          trimStepsRemaining = (pulseG * stepsPerGram) * activeMicrosteps;
          pump.setCurrentPosition(0);
          pump.setSpeed(TRICKLE_SPEED);
          Serial.print("-> Micro-Pulse: +");
          Serial.print(pulseG, 2);
          Serial.println("g");
        } else {
          currentState = STATE_SUCK_BACK;
        }
      }
      break;

    case STATE_TRIM_PULSE:
      if (pump.currentPosition() >= trimStepsRemaining) {
        pump.stop();
        stopTime = millis(); // Enforce guard timer start
        currentState = STATE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else {
        pump.runSpeed();
      }
      break;

    case STATE_SETTLE_TRIM:
      if (isScaleSettled()) {
        // Safety Cut-off: If remaining is under a single drop (0.04g), halt immediately
        if (remaining <= 0.04) {
          Serial.println("-> Remaining weight <= 0.04g (within 1 drop). Halting dispense.");
          currentState = STATE_SUCK_BACK;
        } else if (delivered < targetWeight) {
          currentState = STATE_TRIM_PULSE;
          activeMicrosteps = isHighViscosity ? 16 : 64;
          setMicrostepping(activeMicrosteps);
          
          float stepsPerGram = isHighViscosity ? stepsPerGramHigh : stepsPerGramLow;
          
          float pulseG = max(0.04f, remaining * 0.5f);
          pulseG = min(pulseG, remaining);
          
          trimStepsRemaining = (pulseG * stepsPerGram) * activeMicrosteps;
          pump.setCurrentPosition(0);
          pump.setSpeed(TRICKLE_SPEED);
          Serial.print("-> Micro-Pulse: +");
          Serial.print(pulseG, 2);
          Serial.println("g");
        } else {
          currentState = STATE_SUCK_BACK;
        }
      }
      break;

    case STATE_SUCK_BACK: {
      // Dynamic Retraction setup
      activeMicrosteps = isHighViscosity ? 16 : 64;
      setMicrostepping(activeMicrosteps);
      
      // Glycerol gets a massive 1-Rev pressure relief, water gets a standard quick retraction
      long retractMicrosteps = isHighViscosity ? 3200 : 150; // 3200 at 1/16 = 1 full revolution!
      float retractSpeed = isHighViscosity ? -16000.0 : -1200.0;
      
      if (isHighViscosity) {
        Serial.println("-> High Viscosity: Performing rapid 1-Rev pressure relief at 1/16...");
      } else {
        Serial.println("-> Standard Retraction at 1/64...");
      }
      
      trimStepsRemaining = retractMicrosteps;
      pump.setCurrentPosition(0);
      pump.setSpeed(retractSpeed);
      currentState = STATE_RETRACTING;
      break;
    }

    case STATE_RETRACTING:
      if (abs(pump.currentPosition()) >= trimStepsRemaining) {
        pump.stop();
        digitalWrite(enPin, HIGH); // Disable motor driver to avoid heating
        currentState = STATE_COMPLETE;
        Serial.print("Dispense Finished successfully at: ");
        Serial.print(delivered, 2);
        Serial.println("g!");
      } else {
        pump.runSpeed();
      }
      break;

    case STATE_COMPLETE:
      currentState = STATE_IDLE;
      break;

    default:
      break;
  }
}

/**
 * Adaptive variance-based settling slope detector.
 * - Enforces dynamic guard window (1500ms for Glycerol, 1200ms for Water).
 * - Only runs when a fresh scale packet has been parsed.
 */
bool isScaleSettled() {
  unsigned long settleGuardTime = isHighViscosity ? 1500 : 1200;

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
    if (abs(currentWeight - lastSettleWeight) < 0.005) { 
      return true;
    }
    lastSettleWeight = currentWeight;
    settleTimer = millis();
  }
  return false;
}

void processScaleData(String raw) {
  String cleanStr = "";
  for (unsigned int i = 0; i < raw.length(); i++) {
    char ch = raw.charAt(i);
    if (isDigit(ch) || ch == '.' || ch == '-') cleanStr += ch;
  }
  if (cleanStr.length() > 0) {
    currentWeight = cleanStr.toFloat();
    lastScaleUpdateTime = millis();
    newScaleData = true; // Signal that new weight data has arrived

    // Print real-time telemetry over USB Serial (9600 Baud)
    Serial.print("TELEMETRY:");
    Serial.print(currentWeight, 2);
    Serial.print(",");
    Serial.print((int)currentState);
    Serial.print(",");
    Serial.println(isHighViscosity ? "1" : "0");
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
          pump.stop(); 
          digitalWrite(enPin, HIGH); 
          currentState = STATE_IDLE;
          Serial.println("!!! EMERGENCY PUMP HALT !!!");
        } 
        
        // --- Viscosity Mode: HIGH (Glycerol) ---
        else if (usbBuffer.equalsIgnoreCase("H")) {
          isHighViscosity = true;
          activeMicrosteps = 16;
          setMicrostepping(activeMicrosteps);
          Serial.println("Viscosity Mode: HIGH (Glycerol settings applied).");
          Serial.print("Active steps/g: ");
          Serial.println(stepsPerGramHigh, 1);
        }
        
        // --- Viscosity Mode: LOW (Water) ---
        else if (usbBuffer.equalsIgnoreCase("L")) {
          isHighViscosity = false;
          activeMicrosteps = 64;
          setMicrostepping(activeMicrosteps);
          Serial.println("Viscosity Mode: LOW (Water settings applied).");
          Serial.print("Active steps/g: ");
          Serial.println(stepsPerGramLow, 1);
        }
        
        // --- Calibration Input State ---
        else if (currentState == STATE_CALIBRATE_WAIT_INPUT) {
          float measuredWeight = usbBuffer.toFloat();
          if (measuredWeight > 0.05) {
            // 10000 steps were taken at 1/16 microstepping = 625 Full Steps
            float fullStepsTaken = 10000.0 / 16.0;
            float calculatedSteps = fullStepsTaken / measuredWeight;
            
            if (isHighViscosity) {
              stepsPerGramHigh = calculatedSteps;
            } else {
              stepsPerGramLow = calculatedSteps;
            }
            
            Serial.println("\n========================================");
            Serial.println("Calibration Successful!");
            Serial.print("Measured weight: ");
            Serial.print(measuredWeight, 2);
            Serial.println("g");
            Serial.print("New steps/gram [");
            Serial.print(isHighViscosity ? "Glycerol" : "Water");
            Serial.print("]: ");
            Serial.println(calculatedSteps, 2);
            Serial.println("========================================");
            
            currentState = STATE_IDLE;
          } else {
            Serial.println("Error: Invalid weight entered. Please enter a valid weight > 0.05g:");
          }
        }
        
        // --- Calibration Mode Activation ---
        else if (usbBuffer.equalsIgnoreCase("C")) {
          if (currentState == STATE_IDLE) {
            digitalWrite(enPin, LOW); // Enable motor driver
            activeMicrosteps = 16; // Use 1/16 microstepping for calibration accuracy
            setMicrostepping(activeMicrosteps);
            
            pump.setCurrentPosition(0);
            pump.moveTo(10000); // Dispense exactly 10000 steps
            pump.setSpeed(CALIBRATE_SPEED);
            
            currentState = STATE_CALIBRATE_RUN;
            Serial.println("\n-> Starting calibration run.");
            Serial.println("-> Dispensing exactly 10000 microsteps (625 full steps) at 1/16 step...");
          } else {
            Serial.println("Error: Can only calibrate when pump is in IDLE.");
          }
        }
        
        // --- Start standard Dispense Target ---
        else {
          float target = usbBuffer.toFloat();
          if (target > 1.5) { // Target must be greater than bulk threshold
            digitalWrite(enPin, LOW); // Enable motor driver
            targetWeight = target - TARGET_MARGIN; // Apply safety margin internally
            startWeight = currentWeight;
            lastScaleUpdateTime = millis();
            
            float stepsPerGram = isHighViscosity ? stepsPerGramHigh : stepsPerGramLow;
            
            // Calculate bulk target steps (85% of adjusted target)
            float bulkTargetG = targetWeight * 0.85; 
            activeMicrosteps = 8; // Use 1/8 microstepping for high-speed bulk fill
            setMicrostepping(activeMicrosteps);
            
            bulkEndSteps = (bulkTargetG * stepsPerGram) * activeMicrosteps;
            pump.setCurrentPosition(0);
            pump.setSpeed(BULK_SPEED);
            
            currentState = STATE_BULK_FILL;
            Serial.print("\nStarting Dispense. Target: ");
            Serial.print(target, 2); // Print user's requested target
            Serial.print("g (Adjusted target: ");
            Serial.print(targetWeight, 2);
            Serial.println("g due to safety margin)");
            Serial.print("-> Aiming for ");
            Serial.print(bulkTargetG, 2);
            Serial.print("g bulk fill (");
            Serial.print(bulkEndSteps);
            Serial.println(" steps at 1/8 microstep)");
          } else {
            Serial.println("Error: Enter target weight > 1.50g, 'H' for Glycerol, 'L' for Water, or 'C' to calibrate.");
          }
        }
        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) usbBuffer += c;
  }
}

// standalone TMC2209 microstep configuration via MS1/MS2 pins
void setMicrostepping(int resolution) {
  if (resolution == 8)  { digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, LOW);  }
  else if (resolution == 16) { digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, LOW);  } // Correct for TMC2209 Standalone Mode!
  else if (resolution == 64) { digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, HIGH); }
  else { // Fallback to 1/16
    digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, LOW); 
  }
}
