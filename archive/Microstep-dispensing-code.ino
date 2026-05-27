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

// --- Calibration Constants ---
// y = mx + b (where y = grams, x = microsteps)
const float slope = 0.0000862;
const float y_intercept = -0.0316667;

void setup() {
  Serial.begin(9600);
  
  // Initialize MS pins as outputs
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  pinMode(ms3Pin, OUTPUT);
  
  // Set the A4988 to 1/16th microstepping mode to match your calibration curve
  setMicrostepping(16);
  
  // Optimal speeds for microstepping (Arduino maxes out around 4000 steps/sec)
  pumpMotor.setMaxSpeed(4000);     
  pumpMotor.setAcceleration(1000); 
  
  Serial.println("=========================================");
  Serial.println("    Gravimetric Dispenser Active         ");
  Serial.println("=========================================");
  Serial.println("Type a target weight in grams (e.g., 5.5) and press Enter:");
}

void loop() {
  // Check if you have typed something into the Serial Monitor
  if (Serial.available() > 0) {
    
    // Read the number inputted by the user
    float targetGrams = Serial.parseFloat();
    
    // This if-statement prevents it from randomly running 0-gram dispenses
    if (targetGrams > 0.0) {
      
      // 1. Calculate the required MICROSTEPS using your inverted equation: x = (y - b) / m
      float exactMicroSteps = (targetGrams - y_intercept) / slope;
      
      // 2. A stepper motor can't take a fraction of a step, so round to the nearest whole number
      long requiredSteps = round(exactMicroSteps);
      
      Serial.print("Target: ");
      Serial.print(targetGrams);
      Serial.print("g  -->  Calculated Microsteps: ");
      Serial.println(requiredSteps);
      Serial.println("Dispensing...");
      
      // 3. Reset position to 0 and move the calculated amount
      pumpMotor.setCurrentPosition(0);
      pumpMotor.moveTo(requiredSteps);
      
      // 4. Run the motor until the target is reached
      while (pumpMotor.distanceToGo() != 0) {
        pumpMotor.run();
      }
      
      Serial.println(">> Dispense Complete <<");
      Serial.println("Enter the next target weight in grams:");
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
      Serial.println("Hardware Mode: FULL STEP");
      break;
    case 2: // 1/2 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, LOW);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Hardware Mode: 1/2 STEP");
      break;
    case 4: // 1/4 Step
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Hardware Mode: 1/4 STEP");
      break;
    case 8: // 1/8 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, LOW);
      Serial.println("Hardware Mode: 1/8 STEP");
      break;
    case 16: // 1/16 Step
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, HIGH);
      digitalWrite(ms3Pin, HIGH);
      Serial.println("Hardware Mode: 1/16 STEP");
      break;
    default:
      Serial.println("Invalid mode. Defaulting to FULL STEP.");
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, LOW);
      digitalWrite(ms3Pin, LOW);
      break;
  }
}