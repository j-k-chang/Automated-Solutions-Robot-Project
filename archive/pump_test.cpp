#include <Arduino.h>
#include <AccelStepper.h>

// Pin definitions
const int stepPin = 2;
const int dirPin = 3;
const int enPin = 4;
const int ms1Pin = 5;
const int ms2Pin = 6;

// Create AccelStepper object
// AccelStepper::DRIVER means it uses standard step and direction pins
AccelStepper stepper(AccelStepper::DRIVER, stepPin, dirPin);

// ==========================================
// USER CONFIGURATION
// ==========================================
// Set the microstepping resolution. 
// Valid options for TMC2209 standalone: 8, 16, 32, 64
const int MICROSTEPS = 8; 

// Initial Speed & Acceleration Configuration
float currentMaxSpeed = 4000.0;
float currentAcceleration = 1000.0;
// ==========================================

void setMicrostepping(int resolution) {
  // TMC2209 Standalone Truth Table
  switch(resolution) {
    case 8:
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, LOW);
      Serial.println("Microstepping set to 1/8");
      break;
    case 16:
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, LOW);
      Serial.println("Microstepping set to 1/16");
      break;
    case 32:
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, HIGH);
      Serial.println("Microstepping set to 1/32");
      break;
    case 64:
      digitalWrite(ms1Pin, HIGH);
      digitalWrite(ms2Pin, HIGH);
      Serial.println("Microstepping set to 1/64");
      break;
    default:
      digitalWrite(ms1Pin, LOW);
      digitalWrite(ms2Pin, LOW);
      Serial.println("Invalid resolution! Defaulting to 1/8");
      break;
  }
}

void printMenu() {
  Serial.println("\n=== AccelStepper Control Menu ===");
  Serial.println("Send commands via Serial Monitor:");
  Serial.println("  V<number> - Set Speed (steps/second) e.g., V1500");
  Serial.println("  A<number> - Set Acceleration (steps/sec^2) e.g., A500");
  Serial.println("  M<number> - Move relative steps (pos/neg) e.g., M800 or M-800");
  Serial.println("  S         - Stop immediately with deceleration");
  Serial.println("  ?         - Print this menu");
  Serial.println("=================================");
  Serial.print("Current Speed: "); Serial.println(currentMaxSpeed);
  Serial.print("Current Accel: "); Serial.println(currentAcceleration);
}

void setup() {
  Serial.begin(9600);
  
  // Configure additional hardware pins
  pinMode(enPin, OUTPUT);
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  
  // Enable the TMC2209 driver
  digitalWrite(enPin, LOW);
  
  // Apply microstepping
  setMicrostepping(MICROSTEPS);
  
  // Configure AccelStepper settings
  stepper.setMaxSpeed(currentMaxSpeed);
  stepper.setAcceleration(currentAcceleration);
  
  delay(100);
  Serial.println("\nTMC2209 Motor Ready (AccelStepper Mode).");
  printMenu();
}

void loop() {
  // This function must be called as frequently as possible for smooth motion
  stepper.run();

  // Check for incoming Serial commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // Ignore whitespaces and newlines
    if (cmd == '\n' || cmd == '\r' || cmd == ' ') return;

    if (cmd == 'S' || cmd == 's') {
      stepper.stop(); // Calculates target position to stop with deceleration
      Serial.println("\n*** STOPPING ***");
      // Clear the rest of the serial buffer
      while(Serial.available() > 0) Serial.read();
      
    } else if (cmd == 'V' || cmd == 'v') {
      float v = Serial.parseFloat();
      if (v > 0) {
        currentMaxSpeed = v;
        stepper.setMaxSpeed(currentMaxSpeed);
        Serial.print("\n-> Max Speed set to: ");
        Serial.println(currentMaxSpeed);
      }
      
    } else if (cmd == 'A' || cmd == 'a') {
      float a = Serial.parseFloat();
      if (a > 0) {
        currentAcceleration = a;
        stepper.setAcceleration(currentAcceleration);
        Serial.print("\n-> Acceleration set to: ");
        Serial.println(currentAcceleration);
      }
      
    } else if (cmd == 'M' || cmd == 'm') {
      long m = Serial.parseInt();
      stepper.move(m);
      Serial.print("\n-> Moving ");
      Serial.print(m);
      Serial.println(" steps...");
      
    } else if (cmd == '?') {
      printMenu();
    }
  }
}
