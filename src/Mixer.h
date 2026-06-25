#ifndef MIXER_H
#define MIXER_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <TMCStepper.h>
#include <mbed.h>

class Mixer {
public:
    // --- State Definition ---
    enum MixerState {
        STATE_IDLE,
        STATE_RUNNING_CONTINUOUS,
        STATE_STOPPING
    };

    // --- Constructor ---
    // Pins: Step = 52, Dir = 48, Enable = 50
    Mixer(int stepPin = 52, int dirPin = 48, int enPin = 50);

    // --- Lifecycle Methods ---
    void begin();
    void update();

    // --- Control Commands ---
    void startContinuous();
    void stop();
    void startAutoRampTest(float startRPM = 100.0f, float targetRPM = 200.0f, float stepRPM = 25.0f, unsigned long intervalMs = 2000);

    // --- Settings & Status ---
    MixerState getState() const { return _currentState; }
    const char* getStateString() const;
    
    void setTargetRPM(float rpm);
    float getTargetRPM() const { return _targetRPM; }
    
    void setAcceleration(float accel);
    float getAcceleration() const { return _acceleration; }
    bool isAutoRamping() const { return _isAutoRamping; }
    
    bool checkUARTConnection();
    uint16_t getDriverMicrosteps();
    uint16_t getDriverCurrent();
    uint32_t getGCONF();
    uint32_t getIOIN();

private:
    // Pins
    int _stepPin;
    int _dirPin;
    int _enPin;

    // AccelStepper instance
    AccelStepper _stepper;

    // Timer-driven STEP output for continuous mixer rotation.
    mbed::Ticker* _stepTicker;
    mbed::DigitalOut* _stepOut;
    volatile bool _stepLevel;

    // TMC2209 Driver UART interface
    TMC2209Stepper _driver;

    // State
    MixerState _currentState;
    float _targetRPM;
    float _acceleration;

    // Auto-ramping test settings
    bool _isAutoRamping;
    float _autoRampTargetRPM;
    float _autoRampStepRPM;
    unsigned long _autoRampIntervalMs;
    unsigned long _lastRampTimeMs;

    void applyStepFrequency();
    void applyStepFrequency(float rpm);
    void stopStepOutput();
    void toggleStep();

    // Constants
    static constexpr float STEPS_PER_REV = 1600.0f; // 1/8 microstepping on 1.8 deg motor
    static constexpr float STANDARD_SPEED_STEPS_SEC = 5333.33f; // 200 RPM (at 1600 steps/rev)
    static constexpr float STANDARD_ACCEL_STEPS_SEC2 = 761.90f;  // Gradual 7-second ramp-up to 200 RPM to prevent stalling
};

#endif // MIXER_H
