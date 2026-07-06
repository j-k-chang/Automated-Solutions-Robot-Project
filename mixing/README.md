# mixing/ — Legacy Mixer Implementation

This directory contains an alternative Mixer implementation using `AccelStepper::run()` with `moveTo(1e9)` for continuous rotation.

**This implementation is NOT used in production.** It was tried but caused stepper stalling when scale data arrived over Serial1, because the scale interrupt competed with AccelStepper's step processing.

The production implementation is in `src/Mixer.cpp`, which uses `mbed::Ticker` + `mbed::DigitalOut` for hardware-timer-driven STEP pulses. This approach is immune to scale data interrupts because the Ticker runs at a fixed hardware interval independent of Serial1 activity.

Keep this directory for reference only. Do not compile it with the main project.
