#ifndef MIXER_H
#define MIXER_H

#include <Arduino.h>
#include <TMCStepper.h>
#include <mbed.h>
#include <new>

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
    ~Mixer();

    // --- Lifecycle Methods ---
    void begin();
    void update();

    // --- Control Commands ---
    void startContinuous();
    void stop();
    void startAutoRampTest(float startRPM = 100.0f, float targetRPM = 150.0f, float stepRPM = 25.0f, unsigned long intervalMs = 2000);

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

    // Timer-driven STEP output.
    // DigitalOut is constructed in begin() (placement new) — not in the Mixer ctor —
    // because mixer is a global and digitalPinToPinName is unsafe during static init.
    alignas(mbed::DigitalOut) unsigned char _stepOutStorage[sizeof(mbed::DigitalOut)];
    mbed::DigitalOut* _stepOut;
    mbed::Ticker _stepTicker;
    volatile bool _stepLevel;
    bool _stepOutReady;

    // TMC2209 Driver UART interface
    TMC2209Stepper _driver;

    // State
    MixerState _currentState;
    float _targetRPM;
    float _currentRPM;     // actual commanded rate (ramps toward _targetRPM)
    float _lastAppliedRPM; // last rate written to the ticker
    float _acceleration;   // steps/sec^2
    unsigned long _lastAccelMs;

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
    void updateSpeedRamp();

    // Constants
    static constexpr float STEPS_PER_REV = 1600.0f; // 1/8 microstepping on 1.8 deg motor
    static constexpr float STANDARD_SPEED_STEPS_SEC = 4000.0f; // 150 RPM (at 1600 steps/rev)
    static constexpr float STANDARD_ACCEL_STEPS_SEC2 = 761.90f;  // Soft ramp (~same accel as before)
    static constexpr float MIN_RAMP_RPM = 10.0f;
};

#endif // MIXER_H
