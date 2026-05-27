#include <Arduino.h>
#include <AccelStepper.h>

// --- Pin Definitions ---
const int stepPin = 2;
const int dirPin = 3;
const int enPin = 4;
const int ms1Pin = 5;
const int ms2Pin = 6;

// --- Communication Settings ---
const long SCALE_BAUD = 9600;
const long USB_BAUD = 115200; 

// --- Dispensing Configuration (Adjusted for 210ms scale lag) ---
float initialSpeed = 3000.0;    
const float approachSpeed = 800.0;
const float trickleSpeed = 120.0;  // Slightly slower for better 200ms resolution
const long suckBackSteps = 250;    // Increased to ensure clean break
const float suckBackSpeed = 800.0; 
const float stopOffset = 0.08;     // Increased to account for 210ms lag at trickle speed

// --- State Machine ---
enum State { IDLE, FAST_FILL, APPROACH, TRICKLE, SUCK_BACK, ERROR_STATE };
State currentState = IDLE;

// --- Global Variables ---
AccelStepper pump(AccelStepper::DRIVER, stepPin, dirPin);
float targetWeight = 0.0;
float currentWeight = 0.0;
float startWeight = 0.0;
unsigned long lastScaleUpdateTime = 0;
String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

// --- Prototypes ---
void processScaleData(String raw);
void handleUsbCommands();
void setMicrostepping(int resolution);

void setup() {
  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD); // Hardware UART on Pins 0/1

  pinMode(enPin, OUTPUT);
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  
  digitalWrite(enPin, LOW); 
  setMicrostepping(16); // Corrected table for TMC2209

  pump.setMaxSpeed(10000);
  pump.setAcceleration(2500);

  Serial.println("\n========================================");
  Serial.println("Gravimetric Feedback System (210ms Lag Optimized)");
  Serial.println("Baud Rate: 115200 | Mode: Plus-Delimiter");
  Serial.println("Commands:");
  Serial.println("  [number]  - Set target and START");
  Serial.println("  V[number] - Set initial speed");
  Serial.println("  S         - EMERGENCY STOP (HARD)");
  Serial.println("========================================");
}

void loop() {
  // 1. Plus-Delimiter Scale Reader (Optimized for no-newline streams)
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

  // 2. Watchdog: Trigger if scale is silent for >1000ms (5 missed packets)
  if (currentState != IDLE && currentState != ERROR_STATE) {
    if (millis() - lastScaleUpdateTime > 1000) {
      pump.stop();
      digitalWrite(enPin, HIGH); // Hard Disable
      currentState = ERROR_STATE;
      Serial.println("\n!!! ERROR: SCALE TIMEOUT - MOTOR CUT !!!");
    }
  }

  // 3. State Machine Logic
  float delivered = currentWeight - startWeight;
  float remaining = targetWeight - delivered;

  switch (currentState) {
    case IDLE:
      break;

    case FAST_FILL:
      if (remaining <= 8.0) { // Thresholds widened for 200ms lag
        currentState = APPROACH;
        pump.setSpeed(approachSpeed);
        Serial.println("-> State: APPROACH");
      } else {
        pump.runSpeed();
      }
      break;

    case APPROACH:
      if (remaining <= 1.2) { // Thresholds widened for 200ms lag
        currentState = TRICKLE;
        pump.setSpeed(trickleSpeed);
        Serial.println("-> State: TRICKLE");
      } else {
        pump.runSpeed();
      }
      break;

    case TRICKLE:
      // In TRICKLE, we stop at targetWeight - stopOffset
      if (remaining <= stopOffset) {
        pump.setCurrentPosition(0);
        pump.moveTo(-suckBackSteps);
        pump.setSpeed(-suckBackSpeed); 
        currentState = SUCK_BACK;
        Serial.println("-> Target hit. Performing SUCK_BACK...");
      } else {
        pump.runSpeed();
      }
      break;

    case SUCK_BACK:
      if (pump.distanceToGo() != 0) {
        pump.runSpeedToPosition();
      } else {
        Serial.println("Dispense Complete.");
        currentState = IDLE;
      }
      break;

    case ERROR_STATE:
      break;
  }
}

void processScaleData(String raw) {
  String cleanStr = "";
  for (unsigned int i = 0; i < raw.length(); i++) {
    char ch = raw.charAt(i);
    if (isDigit(ch) || ch == '.' || ch == '-') {
      cleanStr += ch;
    }
  }

  if (cleanStr.length() > 0) {
    currentWeight = cleanStr.toFloat();
    lastScaleUpdateTime = millis();
    
    // Status broadcast (Non-blocking)
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 400) { // Adjusted to match scale cycle
      if (currentState != IDLE && currentState != ERROR_STATE) {
        Serial.print("Net: ");
        Serial.print(currentWeight - startWeight, 2);
        Serial.print("g | Rem: ");
        Serial.print(targetWeight - (currentWeight - startWeight), 2);
        Serial.println("g");
      }
      lastPrint = millis();
    }
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
          digitalWrite(enPin, HIGH); // Hard Disable
          currentState = IDLE;
          Serial.println("\n!!! EMERGENCY STOP: MOTOR DISABLED !!!");
        } 
        else if (usbBuffer.startsWith("V") || usbBuffer.startsWith("v")) {
          float v = usbBuffer.substring(1).toFloat();
          if (v > 0) {
            initialSpeed = v;
            Serial.print("Initial Speed: ");
            Serial.println(initialSpeed);
          }
        } 
        else {
          float target = usbBuffer.toFloat();
          if (target > 0) {
            digitalWrite(enPin, LOW); // Re-enable motor
            targetWeight = target;
            startWeight = currentWeight;
            lastScaleUpdateTime = millis(); // Initialize watchdog
            pump.setSpeed(initialSpeed);    // Set speed once
            currentState = FAST_FILL;
            Serial.print("Starting Dispense: ");
            Serial.print(targetWeight);
            Serial.println("g");
          }
        }
        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) {
      usbBuffer += c;
    }
  }
}

void setMicrostepping(int resolution) {
  // Corrected Standalone TMC2209 Truth Table
  switch(resolution) {
    case 8:  digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, LOW);  break;
    case 32: digitalWrite(ms1Pin, LOW);  digitalWrite(ms2Pin, HIGH); break;
    case 64: digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, LOW);  break;
    case 16: digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, HIGH); break;
  }
}
