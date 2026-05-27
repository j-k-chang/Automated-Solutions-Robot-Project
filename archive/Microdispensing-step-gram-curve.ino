#include <Arduino.h>
#include <AccelStepper.h>

// Define Arduino pin connections
const int dirPin = 3;
const int stepPin = 4;

// Define Microstepping control pins
const int ms1Pin = 5;
const int ms2Pin = 6;
const int ms3Pin = 7;

#define motorInterfaceType 1

// Create a new instance of the AccelStepper class
AccelStepper pumpMotor(motorInterfaceType, stepPin, dirPin);

// --- Calibration Parameters ---
// IMPORTANT: In 1/16th mode, 1 revolution = 3200 steps (instead of 200)
// You may need to increase these targets to dispense enough liquid to weigh!
long currentTargetSteps = 6400;  // Starting target steps (1 full rev at 1/16th)
long stepIncrement = 3200;       // Steps to add for each subsequent run
long maxTargetSteps = 32000;     // Stop sequence after this many steps
int readDelay = 6000;           // Time in milliseconds (6s) to read the scale

void setup() {
  Serial.begin(9600);
  
  // Initialize MS pins as outputs
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  pinMode(ms3Pin, OUTPUT);
  
  // Set the microstepping mode before enabling the motor
  // Options: 1 (Full), 2 (Half), 4 (Quarter), 8 (Eighth), 16 (Sixteenth)
  setMicrostepping(16); 
  
  // Conservative speeds for high-torque calibration
  pumpMotor.setMaxSpeed(4000);    // Max reliable speed for Arduino
  pumpMotor.setAcceleration(2000); 
  
  Serial.println("=========================================");
  Serial.println(" Gravimetric Calibration Sequence Ready  ");
  Serial.println("=========================================");
  Serial.println("WARNING: Ensure the pump tube is fully primed.");
  Serial.println("Starting in 5 seconds...");
  Serial.println("-----------------------------------------");
  delay(5000);
}

void loop() {
  // Only run if we haven't hit our maximum test limit
  if (currentTargetSteps <= maxTargetSteps) {
    
    Serial.print("COMMAND: Dispensing ");
    Serial.print(currentTargetSteps);
    Serial.println(" steps.");
    
    // Reset position to 0 so moveTo() acts as a relative distance
    pumpMotor.setCurrentPosition(0);
    pumpMotor.moveTo(currentTargetSteps);
    
    // Run the motor until the target is reached
    while (pumpMotor.distanceToGo() != 0) {
      pumpMotor.run();
    }
    
    // Alert the user to read the scale
    Serial.println(">> DISPENSE COMPLETE <<");
    Serial.print(">>> RECORD WEIGHT FOR ");
    Serial.print(currentTargetSteps);
    Serial.println(" STEPS NOW <<<");
    
    Serial.print("Waiting ");
    Serial.print(readDelay / 1000);
    Serial.println(" seconds before the next run...");
    Serial.println("-----------------------------------------");
    
    // Wait for the scale to settle and the user to log the data
    delay(readDelay);
    
    // Increase the target steps for the next loop iteration
    currentTargetSteps += stepIncrement; 
    
  } else {
    // End of calibration sequence
    Serial.println("Calibration sequence complete.");
    Serial.println("Plot your 'Steps' (X-axis) vs 'Grams' (Y-axis) to find the slope.");
    
    // Infinite loop to halt the program
    while (true) {
      delay(1000); 
    }
  }
}

// ---------------------------------------------------------
// HELPER FUNCTION: Change resolution dynamically
// ---------------------------------------------------------
void setMicrostepping(int mode) {
  switch (mode) {
    case 1: // Full Step
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, LOW);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Mode changed to: FULL STEP");
      break;
    case 2: // 1/2 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, LOW);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Mode changed to: 1/2 STEP");
      break;
    case 4: // 1/4 Step
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Mode changed to: 1/4 STEP");
      break;
    case 8: // 1/8 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Mode changed to: 1/8 STEP");
      break;
    case 16: // 1/16 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, HIGH);
      Serial.println("Mode changed to: 1/16 STEP");
      break;
    default:
      Serial.println("Invalid mode selected. Defaulting to FULL STEP.");
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, LOW);
      digitalWrite(ms3Pin, LOW);
      break;
  }
}