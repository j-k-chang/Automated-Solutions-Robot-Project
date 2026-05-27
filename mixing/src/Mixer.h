#ifndef MIXER_H
#define MIXER_H

#include <Arduino.h>
#include <AccelStepper.h>

class Mixer {
public:
    // --- State Definition ---
    enum MixerState {
        STATE_IDLE,
        STATE_RUNNING_CONTINUOUS,
        STATE_STOPPING
    };

    // --- Constructor ---
    // Pins: Step = 10, Dir = 11, Enable = 12
    Mixer(int stepPin = 10, int dirPin = 11, int enPin = 12);

    // --- Lifecycle Methods ---
    void begin();
    void update();

    // --- Control Commands ---
    void startContinuous();
    void stop();

    // --- Settings & Status ---
    MixerState getState() const { return _currentState; }
    const char* getStateString() const;

private:
    // Pins
    int _stepPin;
    int _dirPin;
    int _enPin;

    // AccelStepper instance
    AccelStepper _stepper;

    // State
    MixerState _currentState;

    // Standard Speed & Acceleration configuration
    // Based on 1/16 microstepping (3200 steps/rev for standard 1.8 degree motor)
    // 300 RPM = 5 rev/sec = 1600 steps/sec (standard liquid mixing speed)
    static constexpr float STANDARD_SPEED_STEPS_SEC = 1600.0f;
    static constexpr float STANDARD_ACCEL_STEPS_SEC2 = 400.0f;
};

#endif // MIXER_H
