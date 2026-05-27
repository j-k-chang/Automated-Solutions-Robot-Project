#include "Mixer.h"

Mixer::Mixer(int stepPin, int dirPin, int enPin)
    : _stepPin(stepPin),
      _dirPin(dirPin),
      _enPin(enPin),
      _stepper(AccelStepper::DRIVER, stepPin, dirPin),
      _currentState(STATE_IDLE) {}

void Mixer::begin() {
    // Configure hardware pins
    pinMode(_enPin, OUTPUT);

    // Disable the driver by default to keep it cool and save power
    digitalWrite(_enPin, HIGH);

    // Configure AccelStepper parameters
    _stepper.setMaxSpeed(STANDARD_SPEED_STEPS_SEC);
    _stepper.setAcceleration(STANDARD_ACCEL_STEPS_SEC2);

    _currentState = STATE_IDLE;
}

void Mixer::startContinuous() {
    Serial.println("Mixer: Starting continuous rotation...");
    
    // Enable the stepper driver (Active LOW)
    digitalWrite(_enPin, LOW);
    delayMicroseconds(5); // Small delay to let driver wake up

    _currentState = STATE_RUNNING_CONTINUOUS;

    // Set a very far-off target position to simulate continuous rotation
    // AccelStepper will smoothly accelerate up to standard speed
    _stepper.setMaxSpeed(STANDARD_SPEED_STEPS_SEC);
    _stepper.setAcceleration(STANDARD_ACCEL_STEPS_SEC2);
    _stepper.moveTo(_stepper.currentPosition() + 1000000000L); 
}


void Mixer::stop() {
    if (_currentState == STATE_IDLE) {
        return;
    }

    Serial.println("Mixer: Decelerating to stop...");
    _currentState = STATE_STOPPING;
    _stepper.stop(); // Calculates target position based on current deceleration ramp
}

void Mixer::update() {
    switch (_currentState) {
        case STATE_IDLE:
            // Driver is disabled (EN = HIGH), motor is completely relaxed
            break;

        case STATE_RUNNING_CONTINUOUS:
            // Continuous rotation forward - call run() to process steps with acceleration
            _stepper.run();
            
            // Periodically refresh target position to ensure we never run out of steps
            if (_stepper.distanceToGo() < 500000000L) {
                _stepper.moveTo(_stepper.currentPosition() + 1000000000L);
            }
            break;


        case STATE_STOPPING:
            // Run stepper deceleration ramp
            if (_stepper.run()) {
                // Still decelerating
            } else {
                // Deceleration complete! Disable driver to keep motor and driver cool.
                digitalWrite(_enPin, HIGH);
                _currentState = STATE_IDLE;
                Serial.println("Mixer: Fully stopped and powered down.");
            }
            break;
    }
}

const char* Mixer::getStateString() const {
    switch (_currentState) {
        case STATE_IDLE:               return "IDLE (Coasting/Cool)";
        case STATE_RUNNING_CONTINUOUS: return "RUNNING (Continuous)";
        case STATE_STOPPING:           return "STOPPING (Decelerating)";
        default:                       return "UNKNOWN";
    }
}
