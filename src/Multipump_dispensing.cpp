#include "Multipump_dispensing.h"

namespace MultiPump {

// --- Dispensing Configuration ---
const float initialSpeed = 2000000.0;
const float approachSpeed = 2000.0;
const float trickleSpeed = 1000.0;
const long suckBackSteps = 250;
const float suckBackSpeed = 800.0;

// --- Precision Tuning ---
const float stopOffset = 0.10f;
const unsigned long settleTimeLimit = 500;

// --- Global State ---
AccelStepper pump1(AccelStepper::DRIVER, PUMP1_STEP, PUMP1_DIR);
AccelStepper pump2(AccelStepper::DRIVER, PUMP2_STEP, PUMP2_DIR);

float currentWeight = 0.0f;
unsigned long lastScaleUpdateTime = 0;
unsigned long settleStartTime = 0;

String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

// Sequence state
SequenceState sequenceState = SEQ_PROMPT_PUMP1;
DispenseState dispenseState = FAST_FILL;

float targetPump1 = 0.0f;
float targetPump2 = 0.0f;
float startWeight = 0.0f;
unsigned long lastScaleUpdateTimeLocal = 0;

/**
 * @brief Parses incoming raw data from the scale and updates the current weight.
 * 
 * @param raw The raw string received from the scale via serial communication.
 */
void processScaleData(String raw);

/**
 * @brief Configures the microstepping resolution for a stepper motor driver.
 * 
 * @param pinMS1 The microcontroller pin connected to MS1 on the driver.
 * @param pinMS2 The microcontroller pin connected to MS2 on the driver.
 * @param resolution The desired microstepping resolution (e.g., 16 for 1/16th step).
 */
void setMicrostepping(int pinMS1, int pinMS2, int resolution) {
  if (resolution == 16) {
    digitalWrite(pinMS1, HIGH);
    digitalWrite(pinMS2, HIGH);
  }
}

/**
 * @brief Reads and processes commands sent from the host PC via USB Serial.
 * 
 * @param outTarget Reference to a float where the parsed target weight will be stored.
 * @return true if a valid target weight was received, false otherwise.
 */
bool handleUsbCommands(float &outTarget);

void multipumpSetup() {
  delay(2000); // Allow hardware to stabilize upon boot

  Serial.begin(USB_BAUD);   // Initialize USB serial communication
  Serial1.begin(SCALE_BAUD); // Initialize Scale serial communication

  // Initialize Pump 1 pins
  pinMode(PUMP1_STEP, OUTPUT);
  pinMode(PUMP1_DIR, OUTPUT);
  pinMode(PUMP1_EN, OUTPUT);
  pinMode(PUMP1_MS1, OUTPUT);
  pinMode(PUMP1_MS2, OUTPUT);

  // Initialize Pump 2 pins
  pinMode(PUMP2_STEP, OUTPUT);
  pinMode(PUMP2_DIR, OUTPUT);
  pinMode(PUMP2_EN, OUTPUT);
  pinMode(PUMP2_MS1, OUTPUT);
  pinMode(PUMP2_MS2, OUTPUT);

  // Enable both stepper drivers (LOW means active)
  digitalWrite(PUMP1_EN, LOW);
  digitalWrite(PUMP2_EN, LOW);
  
  // Set both drivers to 1/16th microstepping for smoother operation
  setMicrostepping(PUMP1_MS1, PUMP1_MS2, 16);
  setMicrostepping(PUMP2_MS1, PUMP2_MS2, 16);

  // Configure stepper motor kinematic limits
  pump1.setMaxSpeed(10000);
  pump1.setAcceleration(3000);
  pump2.setMaxSpeed(10000);
  pump2.setAcceleration(3000);

  Serial.println("\n========================================");
  Serial.println("Multi-Pump Gravimetric Dispensing System");
  Serial.println("Pump 1: pins 2,3 | Pump 2: pins 7,8");
  Serial.println("Pre-Stop: 0.10g | Settle Delay: 500ms");
  Serial.println("========================================");
}

/**
 * @brief Core state machine executing the dispensing logic for a single pump.
 * 
 * @param pump The AccelStepper instance representing the pump to control.
 * @param targetWeight The goal weight (in grams) to dispense during this cycle.
 * @param pumpLabel A string label for the pump (e.g., "Pump 1") used in serial logging.
 * @return true if the dispensing cycle is fully complete, false if still in progress.
 */
bool dispensePump(AccelStepper& pump, float targetWeight, const char* pumpLabel);

void multipumpLoop() {
  // Always read incoming data from the digital scale and buffer it
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '+' || c == '-') {
      // Process the buffer when a new weight transmission starts
      if (scaleBuffer.length() > 0) processScaleData(scaleBuffer);
      scaleBuffer = String(c);
    } else if (scaleBuffer.length() < MAX_BUF) {
      scaleBuffer += c;
    }
  }

  // Check for commands or target weights from the host PC
  float pendingTarget = 0.0f;
  bool gotTarget = handleUsbCommands(pendingTarget);

  // Manage the overall multi-pump sequencing workflow
  switch (sequenceState) {
    case SEQ_PROMPT_PUMP1:
      Serial.println("Enter weight (g) for Pump 1:");
      sequenceState = SEQ_DISPENSE_PUMP1;
      break;

    case SEQ_DISPENSE_PUMP1:
      // Start dispensing if a valid target was received from USB
      if (pendingTarget > 0.0f && dispenseState == FAST_FILL) {
        targetPump1 = pendingTarget;
        startWeight = currentWeight;
        lastScaleUpdateTimeLocal = millis();
        pump1.setCurrentPosition(0);
        
        // Ensure only pump 1 is active
        pump2.stop();
        digitalWrite(PUMP2_EN, HIGH); 
        dispenseState = FAST_FILL;
      }

      // Execute the single-pump dispensing logic
      if (dispensePump(pump1, targetPump1 - startWeight, "Pump 1")) {
        Serial.print("Pump 1 done. Dispensed: ");
        Serial.println(currentWeight - startWeight, 2);
        sequenceState = SEQ_PROMPT_PUMP2;
      }
      break;

    case SEQ_PROMPT_PUMP2:
      Serial.println("Enter weight (g) for Pump 2:");
      sequenceState = SEQ_DISPENSE_PUMP2;
      break;

    case SEQ_DISPENSE_PUMP2:
      // Start dispensing if a valid target was received from USB
      if (pendingTarget > 0.0f && dispenseState == FAST_FILL) {
        targetPump2 = pendingTarget;
        startWeight = currentWeight; // Base the dispense against current resting weight
        lastScaleUpdateTimeLocal = millis();
        pump2.setCurrentPosition(0);
        
        // Ensure only pump 2 is active
        pump1.stop();
        digitalWrite(PUMP1_EN, HIGH); 
        dispenseState = FAST_FILL;
      }

      // Execute the single-pump dispensing logic
      if (dispensePump(pump2, targetPump2 - startWeight, "Pump 2")) {
        Serial.print("Pump 2 done. Dispensed: ");
        Serial.println(currentWeight - startWeight, 2);
        sequenceState = SEQ_DONE;
      }
      break;

    case SEQ_DONE:
      Serial.println("\nDispense sequence complete!");
      Serial.println();
      
      // Reset variables to allow for a new dispensing round
      targetPump1 = 0.0f;
      targetPump2 = 0.0f;
      dispenseState = FAST_FILL;
      sequenceState = SEQ_PROMPT_PUMP1;
      break;
  }

  // Safety Feature: Protect against scale disconnection or timeouts while dispensing
  if (sequenceState == SEQ_DISPENSE_PUMP1 || sequenceState == SEQ_DISPENSE_PUMP2) {
    if (millis() - lastScaleUpdateTimeLocal > 1000) {
      pump1.stop();
      pump2.stop();
      digitalWrite(PUMP1_EN, HIGH); // Disable pump 1
      digitalWrite(PUMP2_EN, HIGH); // Disable pump 2
      dispenseState = ERROR_STATE;
      Serial.println("\n!!! ERROR: Scale timeout !!!");
    }
  }
}

bool dispensePump(AccelStepper& pump, float targetWeight, const char* pumpLabel) {
  float delivered = currentWeight - startWeight;
  float remaining = targetWeight - delivered;

  switch (dispenseState) {
    case FAST_FILL:
      // Transition to approach phase when getting close to target (within 10% or 2.0g)
      if (remaining <= (targetWeight * 0.10f) || remaining <= 2.0f) {
        dispenseState = APPROACH;
        pump.setSpeed(approachSpeed);
        Serial.println("  -> State: APPROACH");
      } else {
        pump.runSpeed();
      }
      break;

    case APPROACH:
      // Transition to trickle phase when very close to target (within 1% or 0.2g)
      if (remaining <= (targetWeight * 0.01f) || remaining <= 0.2f) {
        dispenseState = TRICKLE;
        pump.setSpeed(trickleSpeed);
        Serial.println("  -> State: TRICKLE");
      } else {
        pump.runSpeed();
      }
      break;

    case TRICKLE:
      // Stop pumping early to account for fluid already in the air
      if (remaining <= stopOffset) {
        pump.stop();
        settleStartTime = millis();
        dispenseState = SETTLE;
        Serial.println("  -> Braking early. Settling 500ms...");
      } else {
        pump.runSpeed();
      }
      break;

    case SETTLE:
      // Wait for the scale reading to stabilize before evaluating final weight
      if (millis() - settleStartTime >= settleTimeLimit) {
        if (remaining > 0.01f) {
          // If the target is not yet met, pulse the motor slightly
          Serial.print("  Current: "); Serial.print(delivered, 2);
          Serial.print("g | Need: "); Serial.print(remaining, 2);
          Serial.println("g. Pulsing motor...");
          dispenseState = TRICKLE;
          pump.setSpeed(trickleSpeed);
        } else {
          // If the target is met, begin the suck-back sequence to stop dripping
          Serial.print("  Target Met ("); Serial.print(delivered, 2);
          Serial.println("g). Finalizing...");
          pump.setCurrentPosition(0);
          pump.moveTo(-suckBackSteps);
          pump.setSpeed(-suckBackSpeed);
          dispenseState = SUCK_BACK;
        }
      }
      break;

    case SUCK_BACK:
      // Run the motor backwards until the suck-back steps are complete
      if (pump.distanceToGo() != 0) {
        pump.runSpeedToPosition();
      } else {
        dispenseState = FAST_FILL; // Reset state machine for the next dispense cycle
        return true; // Indicates the entire cycle for this pump is complete
      }
      break;

    case ERROR_STATE:
      // Simply return true to escape the loop, error state handled externally
      dispenseState = FAST_FILL;
      return true;

    default:
      break;
  }

  return false;
}

void processScaleData(String raw) {
  String cleanStr = "";
  
  // Clean the raw string to only include digits, decimal points, and minus signs
  for (unsigned int i = 0; i < raw.length(); i++) {
    char ch = raw.charAt(i);
    if (isDigit(ch) || ch == '.' || ch == '-') cleanStr += ch;
  }
  
  if (cleanStr.length() > 0) {
    currentWeight = cleanStr.toFloat();
    lastScaleUpdateTimeLocal = millis();
    
    // Periodically print progress updates to the serial monitor (every 500ms)
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
      DispenseState activeDispense = dispenseState;
      SequenceState activeSeq = sequenceState;
      float currentTarget = 0.0f;
      
      if (activeSeq == SEQ_DISPENSE_PUMP1) currentTarget = targetPump1 - startWeight;
      else if (activeSeq == SEQ_DISPENSE_PUMP2) currentTarget = targetPump2 - startWeight;
      
      // Only print during active dispensing phases
      if ((activeSeq == SEQ_DISPENSE_PUMP1 || activeSeq == SEQ_DISPENSE_PUMP2) &&
          activeDispense != FAST_FILL && activeDispense != ERROR_STATE && activeDispense != SETTLE) {
        Serial.print("  Net: "); Serial.print(currentWeight - startWeight, 2);
        Serial.print("g | Rem: "); Serial.print(currentTarget - (currentWeight - startWeight), 2);
        Serial.println("g");
      }
      lastPrint = millis();
    }
  }
}

bool handleUsbCommands(float &outTarget) {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // Check for end of line indicating command completion
    if (c == '\n' || c == '\r') {
      if (usbBuffer.length() > 0) {
        usbBuffer.trim();
        
        // Check for emergency stop command
        if (usbBuffer.equalsIgnoreCase("S")) {
          pump1.stop();
          pump2.stop();
          digitalWrite(PUMP1_EN, HIGH);
          digitalWrite(PUMP2_EN, HIGH);
          dispenseState = ERROR_STATE;
          sequenceState = SEQ_PROMPT_PUMP1;
          Serial.println("\n!!! STOP !!!");
          usbBuffer = "";
          return false;
        } else {
          // If not a stop command, attempt to parse as a target weight
          float target = usbBuffer.toFloat();
          if (target > 0.0f) {
            outTarget = target;
            usbBuffer = "";
            return true; // Signal that a valid target was received
          }
        }
        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) {
      usbBuffer += c; // Append characters to the buffer
    }
  }
  return false;
}

} // namespace MultiPump
