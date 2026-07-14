# mixing/ — Legacy AccelStepper Mixer

This directory contains an **legacy** Mixer implementation using `AccelStepper::run()` with `moveTo(1e9)` for continuous rotation.

**Do not use this for production.** AccelStepper depends on frequent `run()` calls from the main loop. When scale data arrives over Serial1 (or USB work blocks), step timing jitters and the motor stalls, then recovers — the failure mode seen when AccelStepper was briefly made production.

## Production

The production mixer is in `src/Mixer.cpp` / `src/Mixer.h`:

- `mbed::Ticker` + `mbed::DigitalOut` for hardware-timer-driven STEP pulses (immune to loop/scale latency)
- TMC2209 **UART** on Serial4 for current, microsteps, SpreadCycle, and `intpol`
- Ticker / DigitalOut are **member-owned** (no `new` heap allocation)

Keep this directory for reference only. Do not compile it with the main project.
