#include <Arduino.h>
#include <AccelStepper.h>

/**
 * =================================================================================
 * Gravimetric Dispensing System V6 (Adaptive Progressive Approximation - Strategy 1)
 * =================================================================================
 * This code implements Strategy 1 (Progressive Approximation) for high-precision,
 * zero-overshoot dispensing of any liquid without predefined profiles.
 * 
 * Microstepping Settings (Strictly compliant with TMC2209 Standalone Mode):
 *  - 1/64 Microstep (MS1 = LOW, MS2 = HIGH) for ultra-high calibration and trim resolution.
 *  - 1/8 Microstep (MS1 = LOW, MS2 = LOW) for high-speed volumetric bulk fill.
 * 
 * Key Features:
 *  1. Dynamic Full-Steps-per-Gram (FSpG) Resolution-Independent Calibration.
 *  2. Latency Compensation (combines electronic scale filter lag + fluid fall delay)
 *     to prevent steps-per-gram estimation errors.
 *  3. Conservative Bulk Approximations (stops at 80% to guarantee zero overshoot).
 *  4. Variance-Based Adaptive Settling Slope Detector (eliminates fixed timers).
 *  5. Discrete Micro-Trim Pulses (tops off the vial at trickle speeds with no overshoot).
 *  6. Dynamic Retraction (adjusts suck-back steps based on learned stiction/viscosity).
 */

// --- Physical Pin Out (Matching main.cpp) ---
const int stepPin = 2;
const int dirPin = 3;
const int enPin = 4;
const int ms1Pin = 5;
const int ms2Pin = 6;

// --- Communication Settings ---
const long SCALE_BAUD = 9600;
const long USB_BAUD = 9600; 

// --- Physical System Calibration Constants ---
const float SCALE_LATENCY_SEC = 0.150; // Electronic low-pass scale lag constant
const float STREAM_FALL_TIME_SEC = 0.120; // Time for fluid to travel from nozzle to vial
const float TOTAL_LATENCY_SEC = SCALE_LATENCY_SEC + STREAM_FALL_TIME_SEC; // ~270ms latency

// --- Motor Speed Settings (Microsteps per second) ---
const float CALIBRATE_SPEED = 16000.0; // 16000 microsteps/s at 1/64 = 250 full steps/s
const float BULK_SPEED = 10000.0;      // 10000 microsteps/s at 1/8 = 1250 full steps/s
const float TRICKLE_SPEED = 12000.0;   // 12000 microsteps/s at 1/64 = 187.5 full steps/s

// --- State Machine Definitions ---
enum ControlState {
  STATE_IDLE,
  STATE_BULK_CALIBRATE,
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

float targetWeight = 0.0;
float startWeight = 0.0;
float currentWeight = 0.0;
float lastSettleWeight = 0.0;

unsigned long lastScaleUpdateTime = 0;
unsigned long settleTimer = 0;

int activeMicrosteps = 64;       // Resolution tracking
float fullStepsPerGram = 0.0;    // Dynamic learned calibration (Resolution Independent!)

long bulkEndSteps = 0;
long trimStepsRemaining = 0;

String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

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
  
  activeMicrosteps = 64;
  setMicrostepping(activeMicrosteps); // Default to 1/64 microstep for calibration

  pump.setMaxSpeed(16000);
  pump.setAcceleration(8000);

  Serial.println("\n========================================");
  Serial.println("Progressive Approximation Gravimetric Loop");
  Serial.println("Mode: Resolution-Independent Full-Step Calibration");
  Serial.println("Microstep: 1/64 Calibrate & Trim | 1/8 Bulk Fill");
  Serial.println("========================================");
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

  // Watchdog watchdog safety: Stop pump if scale drops offline during active run
  if (currentState != STATE_IDLE && currentState != STATE_ERROR_STATE && currentState != STATE_SETTLE_BULK && currentState != STATE_SETTLE_TRIM) {
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
    case STATE_BULK_CALIBRATE:
      // Run moderate speed at 1/64 microstepping until 1.00g reached
      if (delivered >= 1.00) {
        // Compensate for latency! Subtract steps taken during system delay
        long latencySteps = CALIBRATE_SPEED * TOTAL_LATENCY_SEC;
        long calMicrosteps = max(100L, pump.currentPosition() - latencySteps);
        
        // Convert microsteps to resolution-independent FULL steps per gram
        fullStepsPerGram = ((float)calMicrosteps / activeMicrosteps) / delivered;
        
        Serial.print("-> Learned Calibration: "); 
        Serial.print(fullStepsPerGram, 1); 
        Serial.println(" Full Steps/gram");
        
        // Calculate steps for bulk target (80% of target weight)
        float bulkTargetG = targetWeight * 0.80;
        float remainingBulkG = bulkTargetG - delivered;
        
        if (remainingBulkG > 0) {
          activeMicrosteps = 8; // Switch to 1/8 step for high-speed bulk fill
          setMicrostepping(activeMicrosteps);
          
          long bulkSteps = (remainingBulkG * fullStepsPerGram) * activeMicrosteps;
          pump.setCurrentPosition(0);
          bulkEndSteps = bulkSteps;
          
          pump.setSpeed(BULK_SPEED);
          currentState = STATE_BULK_FILL;
          Serial.println("-> Entering BULK_FILL (Aiming for 80% target at 1/8 step)");
        } else {
          pump.setSpeed(0);
          currentState = STATE_SETTLE_BULK;
          settleTimer = millis();
          lastSettleWeight = currentWeight;
        }
      } else {
        pump.runSpeed();
      }
      break;

    case STATE_BULK_FILL:
      if (pump.currentPosition() >= bulkEndSteps) {
        pump.stop();
        
        activeMicrosteps = 64; // Restore 1/64 stepping for ultra-high-resolution trim
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
        
        if (delivered < targetWeight) {
          currentState = STATE_TRIM_PULSE;
          activeMicrosteps = 64;
          setMicrostepping(activeMicrosteps);
          
          // Calculate safe tiny pulse: 50% of remaining weight, capped, min 0.04g
          float pulseG = max(0.04f, remaining * 0.5f);
          pulseG = min(pulseG, remaining);
          
          trimStepsRemaining = (pulseG * fullStepsPerGram) * activeMicrosteps;
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
        currentState = STATE_SETTLE_TRIM;
        settleTimer = millis();
        lastSettleWeight = currentWeight;
      } else {
        pump.runSpeed();
      }
      break;

    case STATE_SETTLE_TRIM:
      if (isScaleSettled()) {
        if (delivered < targetWeight) {
          currentState = STATE_TRIM_PULSE;
          activeMicrosteps = 64;
          setMicrostepping(activeMicrosteps);
          
          float pulseG = max(0.04f, remaining * 0.5f);
          pulseG = min(pulseG, remaining);
          
          trimStepsRemaining = (pulseG * fullStepsPerGram) * activeMicrosteps;
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
      // Adaptive Retraction at 1/64 microstepping
      activeMicrosteps = 64;
      setMicrostepping(activeMicrosteps);
      
      long retractMicrosteps = 150;
      float retractSpeed = -1200.0;
      
      // Viscosity load proxy estimation based on steps/gram
      // Dense/viscous liquids (e.g. Honey) have lower steps/gram on volumetric displacement
      if (fullStepsPerGram < 1000.0) { // Highly viscous
        retractMicrosteps = 400;
        retractSpeed = -600.0;
        Serial.println("-> Viscous Liquid detected. Performing deep slow retraction at 1/64...");
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
 * Eliminates static timer delays. Waits until the rate of change is flat (0.00g variance).
 */
bool isScaleSettled() {
  if (millis() - settleTimer > 150) { // Settle window
    if (abs(currentWeight - lastSettleWeight) < 0.005) { // No variance registered
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
  }
}

void handleUsbCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (usbBuffer.length() > 0) {
        usbBuffer.trim();
        if (usbBuffer.equalsIgnoreCase("S")) {
          pump.stop(); 
          digitalWrite(enPin, HIGH); 
          currentState = STATE_IDLE;
          Serial.println("!!! EMERGENCY PUMP HALT !!!");
        } 
        else {
          float target = usbBuffer.toFloat();
          if (target > 1.5) { // Target must be greater than bulk threshold
            digitalWrite(enPin, LOW); // Enable motor driver
            targetWeight = target;
            startWeight = currentWeight;
            lastScaleUpdateTime = millis();
            
            pump.setCurrentPosition(0);
            activeMicrosteps = 64; // High resolution 1/64 calibration
            setMicrostepping(activeMicrosteps);
            pump.setSpeed(CALIBRATE_SPEED);
            
            currentState = STATE_BULK_CALIBRATE;
            Serial.print("\nStarting Dispense. Target: ");
            Serial.print(targetWeight, 2);
            Serial.println("g");
          } else {
            Serial.println("Error: Target weight must be > 1.50g for calibration.");
          }
        }
        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) usbBuffer += c;
  }
}

// standalone TMC2209 microstep configuration via MS1/MS2 pins
// Compliant with standard Trinamic TMC2209 datasheet.
void setMicrostepping(int resolution) {
  if (resolution == 8)  { digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, LOW);  }
  else if (resolution == 16) { digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, HIGH); }
  else if (resolution == 64) { digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, HIGH); }
  else { // Fallback to 1/16
    digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, HIGH); 
  }
}
