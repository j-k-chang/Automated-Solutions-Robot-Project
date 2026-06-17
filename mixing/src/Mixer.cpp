#include "Mixer.h"

Mixer::Mixer(int stepPin, int dirPin, int enPin)
    : _stepPin(stepPin),
      _dirPin(dirPin),
      _enPin(enPin),
      _stepper(AccelStepper::DRIVER, stepPin, dirPin),
      _driver(&Serial4, 0.11f, 0b00), // Hardware Serial4 (Pins 14/15), R_sense = 0.11 Ohm, Address = 0
      _currentState(STATE_IDLE),
      _targetRPM(400.0f),
      _acceleration(STANDARD_ACCEL_STEPS_SEC2),
      _isAutoRamping(false),
      _autoRampTargetRPM(400.0f),
      _autoRampStepRPM(50.0f),
      _autoRampIntervalMs(3000),
      _lastRampTimeMs(0) {}

void Mixer::begin() {
    // Configure hardware pins
    pinMode(_enPin, OUTPUT);

    // Disable the driver by default to keep it cool and save power
    digitalWrite(_enPin, HIGH);

    // Initialize TMC2209 driver UART connection
    Serial4.begin(115200);
    delay(10); // Let Serial1 stabilize
    
    _driver.begin();
    _driver.toff(5);                 // Enable driver
    _driver.rms_current(800);        // Set motor current to 800mA RMS
    _driver.microsteps(8);           // Set 1/8 microstepping (matches hardware pins)
    _driver.en_spreadCycle(true);    // Enable SpreadCycle (high-torque mode, disables StealthChop)
    _driver.pwm_autoscale(true);

    // Configure AccelStepper parameters

    float targetSpeedSteps = (_targetRPM / 60.0f) * STEPS_PER_REV;
    _stepper.setMaxSpeed(targetSpeedSteps);
    _stepper.setAcceleration(_acceleration);

    _currentState = STATE_IDLE;
    _isAutoRamping = false;
}

void Mixer::startContinuous() {
    _isAutoRamping = false; // Disable auto ramp if starting manually
    float targetSpeedSteps = (_targetRPM / 60.0f) * STEPS_PER_REV;
    Serial.print("Mixer: Starting continuous rotation at ");
    Serial.print(_targetRPM);
    Serial.print(" RPM (");
    Serial.print(targetSpeedSteps);
    Serial.println(" steps/sec)...");
    
    // Enable the stepper driver (Active LOW)
    digitalWrite(_enPin, LOW);
    delayMicroseconds(5); // Small delay to let driver wake up

    _currentState = STATE_RUNNING_CONTINUOUS;

    // Set a very far-off target position to simulate continuous rotation
    // AccelStepper will smoothly accelerate up to standard speed
    _stepper.setMaxSpeed(targetSpeedSteps);
    _stepper.setAcceleration(_acceleration);
    _stepper.moveTo(_stepper.currentPosition() + 1000000000L); 
}

void Mixer::startAutoRampTest(float startRPM, float targetRPM, float stepRPM, unsigned long intervalMs) {
    _isAutoRamping = true;
    _autoRampTargetRPM = targetRPM;
    _autoRampStepRPM = stepRPM;
    _autoRampIntervalMs = intervalMs;
    _lastRampTimeMs = millis();
    
    setTargetRPM(startRPM);
    float targetSpeedSteps = (_targetRPM / 60.0f) * STEPS_PER_REV;
    
    // Enable the stepper driver (Active LOW)
    digitalWrite(_enPin, LOW);
    delayMicroseconds(5); // Small delay to let driver wake up

    _currentState = STATE_RUNNING_CONTINUOUS;

    _stepper.setMaxSpeed(targetSpeedSteps);
    _stepper.setAcceleration(_acceleration);
    _stepper.moveTo(_stepper.currentPosition() + 1000000000L);
}


void Mixer::stop() {
    if (_currentState == STATE_IDLE) {
        return;
    }

    Serial.println("Mixer: Decelerating to stop...");
    _currentState = STATE_STOPPING;
    _isAutoRamping = false; // Cancel any active auto ramp
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

    // Handle automatic ramping updates
    if (_currentState == STATE_RUNNING_CONTINUOUS && _isAutoRamping) {
        if (millis() - _lastRampTimeMs >= _autoRampIntervalMs) {
            _lastRampTimeMs = millis();
            if (_targetRPM < _autoRampTargetRPM) {
                setTargetRPM(_targetRPM + _autoRampStepRPM);
                // Keep it silent during active rotation to avoid USB drops
            } else {
                _isAutoRamping = false;
                stop(); // Decelerate to stop when complete, which will print stopped message
            }
        }
    }
}

void Mixer::setTargetRPM(float rpm) {
    if (rpm < 50.0f) rpm = 50.0f;
    if (rpm > 450.0f) rpm = 450.0f; // Capped at 450 RPM max
    _targetRPM = rpm;
    if (_currentState == STATE_RUNNING_CONTINUOUS) {
        float targetSpeedSteps = (_targetRPM / 60.0f) * STEPS_PER_REV;
        _stepper.setMaxSpeed(targetSpeedSteps);
    }
}

void Mixer::setAcceleration(float accel) {
    if (accel < 100.0f) accel = 100.0f;
    if (accel > 20000.0f) accel = 20000.0f;
    _acceleration = accel;
    _stepper.setAcceleration(_acceleration);
}

const char* Mixer::getStateString() const {
    switch (_currentState) {
        case STATE_IDLE:               return "IDLE (Coasting/Cool)";
        case STATE_RUNNING_CONTINUOUS: return "RUNNING (Continuous)";
        case STATE_STOPPING:           return "STOPPING (Decelerating)";
        default:                       return "UNKNOWN";
    }
}

bool Mixer::checkUARTConnection() {
    // Attempt to read the GCONF register from the driver chip.
    // If UART communication is down, it returns 0 or fails.
    uint32_t gconf = _driver.GCONF();
    return (gconf != 0 && gconf != 0xFFFFFFFF);
}

uint16_t Mixer::getDriverMicrosteps() {
    return _driver.microsteps();
}

uint16_t Mixer::getDriverCurrent() {
    return _driver.rms_current();
}

uint32_t Mixer::getGCONF() {
    return _driver.GCONF();
}

uint32_t Mixer::getIOIN() {
    return _driver.IOIN();
}

