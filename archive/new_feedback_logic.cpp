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
const long USB_BAUD = 9600; 

// --- Dispensing Configuration ---
float initialSpeed = 2000000;    
const float approachSpeed = 2000.0; 
const float trickleSpeed = 1000.0;   
const long suckBackSteps = 250;    
const float suckBackSpeed = 800.0; 

// --- Precision Tuning ---
const float stopOffset = 0.10;      
const unsigned long settleTimeLimit = 500; 

enum State { IDLE, FAST_FILL, APPROACH, TRICKLE, SETTLE, SUCK_BACK, ERROR_STATE };
State currentState = IDLE;

AccelStepper pump(AccelStepper::DRIVER, stepPin, dirPin);
float targetWeight = 0.0;
float currentWeight = 0.0;
float startWeight = 0.0;
unsigned long lastScaleUpdateTime = 0;
unsigned long settleStartTime = 0;
String scaleBuffer = "";
String usbBuffer = "";
const unsigned int MAX_BUF = 50;

void processScaleData(String raw);
void handleUsbCommands();
void setMicrostepping(int resolution);

void setup() {
  delay(2000); 
  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD); 

  pinMode(enPin, OUTPUT);
  pinMode(ms1Pin, OUTPUT);
  pinMode(ms2Pin, OUTPUT);
  
  digitalWrite(enPin, LOW); 
  setMicrostepping(16); 

  pump.setMaxSpeed(10000);
  pump.setAcceleration(3000);

  Serial.println("\n========================================");
  Serial.println("Gravimetric Feedback System V5 (Anti-Overshoot)");
  Serial.println("Pre-Stop: 0.10g | Settle Delay: 500ms");
  Serial.println("========================================");
}

void loop() {
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

  if (currentState != IDLE && currentState != ERROR_STATE && currentState != SETTLE) {
    if (millis() - lastScaleUpdateTime > 1000) {
      pump.stop();
      digitalWrite(enPin, HIGH); 
      currentState = ERROR_STATE;
      Serial.println("\n!!! ERROR: SCALE TIMEOUT !!!");
    }
  }

  float delivered = currentWeight - startWeight;
  float remaining = targetWeight - delivered;

  switch (currentState) {
    case FAST_FILL:
      if (remaining <= (targetWeight * 0.10) || remaining <= 2.0) {
        currentState = APPROACH;
        pump.setSpeed(approachSpeed);
        Serial.println("-> State: APPROACH");
      } else {
        pump.runSpeed();
      }
      break;

    case APPROACH:
      if (remaining <= (targetWeight * 0.01) || remaining <= 0.2) {
        currentState = TRICKLE;
        pump.setSpeed(trickleSpeed);
        Serial.println("-> State: TRICKLE");
      } else {
        pump.runSpeed();
      }
      break;

    case TRICKLE:
      if (remaining <= stopOffset) {
        pump.stop();
        settleStartTime = millis();
        currentState = SETTLE;
        Serial.println("-> Braking early. Settling 500ms...");
      } else {
        pump.runSpeed();
      }
      break;

    case SETTLE:
      if (millis() - settleStartTime >= settleTimeLimit) {
        if (remaining > 0.01) { 
          Serial.print("Current: "); Serial.print(delivered, 2); 
          Serial.print("g | Need: "); Serial.print(remaining, 2); 
          Serial.println("g. Pulsing motor...");
          currentState = TRICKLE;
          pump.setSpeed(trickleSpeed);
        } else {
          Serial.print("Target Met ("); Serial.print(delivered, 2); Serial.println("g). Finalizing...");
          pump.setCurrentPosition(0);
          pump.moveTo(-suckBackSteps);
          pump.setSpeed(-suckBackSpeed); 
          currentState = SUCK_BACK;
        }
      }
      break;

    case SUCK_BACK:
      if (pump.distanceToGo() != 0) pump.runSpeedToPosition();
      else {
        Serial.println("Dispense Complete.");
        currentState = IDLE;
      }
      break;
    
    default: break;
  }
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
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
      if (currentState != IDLE && currentState != ERROR_STATE && currentState != SETTLE) {
        Serial.print("Net: "); Serial.print(currentWeight - startWeight, 2);
        Serial.print("g | Rem: "); Serial.print(targetWeight - (currentWeight - startWeight), 2);
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
          pump.stop(); digitalWrite(enPin, HIGH); currentState = IDLE;
          Serial.println("!!! STOP !!!");
        } 
        else {
          float target = usbBuffer.toFloat();
          if (target > 0) {
            digitalWrite(enPin, LOW); 
            targetWeight = target;
            startWeight = currentWeight;
            lastScaleUpdateTime = millis();
            pump.setSpeed(initialSpeed);
            currentState = FAST_FILL;
            Serial.print("Starting: "); Serial.println(targetWeight);
          }
        }
        usbBuffer = "";
      }
    } else if (usbBuffer.length() < MAX_BUF) usbBuffer += c;
  }
}

void setMicrostepping(int resolution) {
  if (resolution == 16) { digitalWrite(ms1Pin, HIGH); digitalWrite(ms2Pin, HIGH); }
}
