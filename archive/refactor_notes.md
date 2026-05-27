# Gravimetric Liquid Dispenser Refactor Notes

## Overview
This document summarizes the changes made to the gravimetric liquid dispenser firmware (running on an Arduino Uno R4 WiFi with a U.S. Solid USS-DBS lab scale). The goal of the refactor was to improve communication, parsing reliability, precision, and safety.

## Key Features Implemented

1. **Plain Text Communication:**
   - The Arduino now listens on the Serial port for incoming plain text commands, replacing the old `ArduinoJson` dependency.
   - Example commands: type a target weight like `50.0` to start dispensing, or `stop` to halt.
   - The old blocking `Serial.parseFloat()` logic has been completely replaced with a fast, non-blocking character buffer and `toFloat()` conversion.

2. **Robust Scale Parsing:**
   - The scale outputs a 14-character string (e.g., `+  12.34 g`).
   - Instead of attempting to parse the entire string blindly, the `scaleBuffer` goes through a strict cleaning loop that extracts only the minus (`-`), plus (`+`), decimal (`.`), and numeric digits before calling `toFloat()`. This effectively ignores any surrounding spaces or `g` symbols.

3. **Settling Time (`SETTLE` State):**
   - After the `TRICKLE` state detects that the target weight is reached, the motor is stopped, and the system transitions into a `SETTLE` state.
   - It waits 250 milliseconds to let the scale reading stabilize.
   - It then re-checks the weight: if it's sufficient, it completes with a `SUCK_BACK` (reversing the pump slightly to prevent dripping); if it ended up slightly under the target weight, it resumes the `TRICKLE` state.

4. **Safety Watchdog:**
   - Each time a valid weight is parsed from `Serial1`, a `lastScaleTime` timer is updated.
   - During any active "pour" state (`FAST_POUR`, `SLOW_POUR`, `TRICKLE`, `SETTLE`), if `millis() - lastScaleTime` exceeds 1000ms (1 second) without a reading, the system immediately halts the pump and transitions to an `ERROR_STATE`.

5. **Status Updates:**
   - A `broadcastStatus()` function provides readable, single-line text output (e.g., `State: FAST_POUR  | Target: 50.00g | Current: 25.00g`).
   - This payload is pushed to `Serial` exactly every 500 milliseconds, providing a clean API for a user connected via Serial Monitor.

## Recent Improvements (April 2026)

6. **Watchdog Timer Fix:**
   - Addressed an issue where sitting idle for more than 1 second before a start command caused an immediate `ERROR_STATE` transition. The `lastScaleTime` is now reset accurately right when the `start` command is received.

7. **Non-Blocking USB Serial:**
   - Replaced blocking `Serial.readStringUntil('\n')` logic with a non-blocking character buffer (`usbBuffer`). This prevents the Arduino from pausing `loop()` operations (and thus stuttering the stepper motor) while waiting for complete commands over USB.

8. **Locked Microstepping (1/16):**
   - Stopped changing MS1/MS2/MS3 pins dynamically on-the-fly during dispensing, which could cause mechanical jumps and lost steps. 
   - The motor driver is now permanently locked into its highest-resolution (1/16) via `setup()`. Step speeds (`fastSpeed` and `suckBackSteps`) were scaled up by 4x to match the new fixed resolution.

9. **Pre-calculated Target Thresholds:**
   - Instead of computing the 90% and 99% logic thresholds on every single cycle of the `loop()`, the `fastPourThreshold` and `slowPourThreshold` are now pre-calculated exactly once upon receiving a start command to improve main loop efficiency.

10. **Memory Safety for Buffers:**
    - Prevented potential heap exhaustion or system crashes caused by runaway strings from noisy serial streams. Both `scaleBuffer` and `usbBuffer` now enforce a strict maximum length limit (`MAX_BUFFER_LENGTH = 128`).

11. **Simplified Command and Control (April 2026):**
    - Completely removed the `ArduinoJson` library dependency from `platformio.ini` and `main.cpp`.
    - Refactored the USB command parser to accept raw float values to initiate a pour, and the "stop" or "x" keywords to cancel a pour, making the system directly usable via the Arduino IDE Serial Monitor without needing a separate client program to construct JSON payloads.
    - Updated status broadcast to output a human-readable string instead of JSON.