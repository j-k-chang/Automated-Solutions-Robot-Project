#include "Mixer.h"
#include <math.h>

Mixer::Mixer(int stepPin, int dirPin, int enPin)
    : _stepPin(stepPin),
      _dirPin(dirPin),
      _enPin(enPin),
      _stepOut(nullptr),
      _stepLevel(false),
      _stepOutReady(false),
      _driver(&Serial4, 0.11f, 0b00), // Hardware Serial4 (Pins 14/15), R_sense = 0.11 Ohm, Address = 0
      _currentState(STATE_IDLE),
      _targetRPM(150.0f),
      _currentRPM(0.0f),
      _lastAppliedRPM(-1.0f),
      _acceleration(STANDARD_ACCEL_STEPS_SEC2),
      _lastAccelMs(0),
      _isAutoRamping(false),
      _autoRampTargetRPM(150.0f),
      _autoRampStepRPM(50.0f),
      _autoRampIntervalMs(3000),
      _lastRampTimeMs(0) {}

Mixer::~Mixer() {
    stopStepOutput();
    if (_stepOutReady && _stepOut) {
        _stepOut->~DigitalOut();
        _stepOut = nullptr;
        _stepOutReady = false;
    }
}

void Mixer::begin() {
    pinMode(_dirPin, OUTPUT);
    pinMode(_enPin, OUTPUT);
    digitalWrite(_dirPin, HIGH);

    // Disable the driver by default to keep it cool and save power
    digitalWrite(_enPin, HIGH);

    // Construct DigitalOut here — not in the global Mixer ctor — so pin mapping is valid.
    if (!_stepOutReady) {
        _stepOut = new (_stepOutStorage) mbed::DigitalOut(digitalPinToPinName(_stepPin), 0);
        _stepOutReady = true;
    }
    stopStepOutput();

    // Initialize TMC2209 driver UART connection
    Serial4.begin(115200);
    delay(10);

    _driver.begin();
    _driver.toff(5);                 // Enable driver
    _driver.rms_current(1000);       // Set motor current to 1000mA RMS
    _driver.microsteps(8);           // 1/8 microstepping; STEPS_PER_REV must match
    _driver.intpol(true);            // Interpolate toward 256 microsteps internally
    _driver.en_spreadCycle(true);    // SpreadCycle (high-torque mode)
    _driver.pwm_autoscale(true);

    _currentState = STATE_IDLE;
    _currentRPM = 0.0f;
    _isAutoRamping = false;
}

void Mixer::applyStepFrequency() {
    applyStepFrequency(_currentRPM > 0.0f ? _currentRPM : _targetRPM);
}

void Mixer::applyStepFrequency(float rpm) {
    if (!_stepOutReady || !_stepOut) {
        return;
    }

    float targetSpeedSteps = (rpm / 60.0f) * STEPS_PER_REV;
    if (targetSpeedSteps < 1.0f) {
        targetSpeedSteps = 1.0f;
    }

    // Toggle twice per step pulse (high + low) → half-period in microseconds
    unsigned long halfPeriodUs = (unsigned long)(500000.0f / targetSpeedSteps);
    if (halfPeriodUs < 2) {
        halfPeriodUs = 2;
    }

    _stepTicker.detach();
    _stepTicker.attach(mbed::callback(this, &Mixer::toggleStep), std::chrono::microseconds(halfPeriodUs));
}

void Mixer::stopStepOutput() {
    _stepTicker.detach();
    _stepLevel = false;
    if (_stepOutReady && _stepOut) {
        *_stepOut = 0;
    }
}

void Mixer::toggleStep() {
    if (!_stepOutReady || !_stepOut) {
        return;
    }
    _stepLevel = !_stepLevel;
    *_stepOut = _stepLevel ? 1 : 0;
}

void Mixer::startContinuous() {
    _isAutoRamping = false;
    float targetSpeedSteps = (_targetRPM / 60.0f) * STEPS_PER_REV;
    Serial.print("Mixer: Starting continuous rotation at ");
    Serial.print(_targetRPM);
    Serial.print(" RPM (");
    Serial.print(targetSpeedSteps);
    Serial.println(" steps/sec), soft-ramping...");

    // Enable the stepper driver (Active LOW)
    digitalWrite(_enPin, LOW);
    delayMicroseconds(5);

    // Soft-start from a low RPM — jumping to _targetRPM immediately stalls under load.
    _currentRPM = MIN_RAMP_RPM;
    _lastAppliedRPM = -1.0f;
    _lastAccelMs = millis();
    applyStepFrequency(_currentRPM);
    _lastAppliedRPM = _currentRPM;
    _currentState = STATE_RUNNING_CONTINUOUS;
}

void Mixer::startAutoRampTest(float startRPM, float targetRPM, float stepRPM, unsigned long intervalMs) {
    _isAutoRamping = true;
    _autoRampTargetRPM = targetRPM;
    _autoRampStepRPM = stepRPM;
    _autoRampIntervalMs = intervalMs;
    _lastRampTimeMs = millis();

    setTargetRPM(startRPM);

    digitalWrite(_enPin, LOW);
    delayMicroseconds(5);

    _currentRPM = MIN_RAMP_RPM;
    _lastAppliedRPM = -1.0f;
    _lastAccelMs = millis();
    applyStepFrequency(_currentRPM);
    _lastAppliedRPM = _currentRPM;
    _currentState = STATE_RUNNING_CONTINUOUS;
}

void Mixer::stop() {
    if (_currentState == STATE_IDLE) {
        return;
    }

    stopStepOutput();
    digitalWrite(_enPin, HIGH);
    _currentState = STATE_IDLE;
    _currentRPM = 0.0f;
    _lastAppliedRPM = -1.0f;
    _isAutoRamping = false;
    Serial.println("Mixer: Fully stopped and powered down.");
}

void Mixer::updateSpeedRamp() {
    if (_currentState != STATE_RUNNING_CONTINUOUS) {
        return;
    }

    unsigned long now = millis();
    unsigned long dtMs = now - _lastAccelMs;
    if (dtMs == 0) {
        return;
    }
    _lastAccelMs = now;

    float rpmPerSec = (_acceleration / STEPS_PER_REV) * 60.0f;
    float deltaRpm = rpmPerSec * (dtMs / 1000.0f);

    if (_currentRPM < _targetRPM) {
        _currentRPM += deltaRpm;
        if (_currentRPM > _targetRPM) {
            _currentRPM = _targetRPM;
        }
    } else if (_currentRPM > _targetRPM) {
        _currentRPM -= deltaRpm;
        if (_currentRPM < _targetRPM) {
            _currentRPM = _targetRPM;
        }
        if (_currentRPM < MIN_RAMP_RPM && _targetRPM >= MIN_RAMP_RPM) {
            _currentRPM = MIN_RAMP_RPM;
        }
    } else {
        return;
    }

    // Only retune ticker when speed changed enough — detach/attach every loop stalls pulses.
    if (fabsf(_currentRPM - _lastAppliedRPM) >= 0.5f) {
        applyStepFrequency(_currentRPM);
        _lastAppliedRPM = _currentRPM;
    }
}

void Mixer::update() {
    switch (_currentState) {
        case STATE_IDLE:
            break;

        case STATE_RUNNING_CONTINUOUS:
            // STEP pulses are generated by mbed::Ticker; only retune rate here.
            updateSpeedRamp();
            break;

        case STATE_STOPPING:
            stop();
            break;
    }

    // Handle automatic ramping updates (test mode: step target RPM over intervals)
    if (_currentState == STATE_RUNNING_CONTINUOUS && _isAutoRamping) {
        if (millis() - _lastRampTimeMs >= _autoRampIntervalMs) {
            _lastRampTimeMs = millis();
            if (_targetRPM < _autoRampTargetRPM) {
                setTargetRPM(_targetRPM + _autoRampStepRPM);
            } else {
                _isAutoRamping = false;
                stop();
            }
        }
    }
}

void Mixer::setTargetRPM(float rpm) {
    if (rpm < 50.0f) rpm = 50.0f;
    if (rpm > 400.0f) rpm = 400.0f;
    _targetRPM = rpm;
    // Actual rate ramps in updateSpeedRamp(); do not jump the ticker here.
}

void Mixer::setAcceleration(float accel) {
    if (accel < 100.0f) accel = 100.0f;
    if (accel > 20000.0f) accel = 20000.0f;
    _acceleration = accel;
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
