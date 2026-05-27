# TMC2209 Stepper Motor Controller with AccelStepper

This project provides an interactive, non-blocking serial interface to control a stepper motor using a TMC2209 driver and the `AccelStepper` Arduino library.

## Features

*   **Smooth Motion Profile:** Utilizes the `AccelStepper` library for smooth acceleration and deceleration curves.
*   **Dynamic Command Interface:** Control speed, acceleration, and movement distances on-the-fly via the Arduino Serial Monitor.
*   **Hardware Microstepping Configuration:** Automatically configures the `MS1` and `MS2` pins for the TMC2209 standalone mode based on a user-defined setting.
*   **Graceful Stop:** Includes a command to immediately decelerate and stop the motor during a move.

## Hardware Setup

This code expects the following pin connections between the Arduino and the TMC2209 driver:

| Arduino Pin | TMC2209 Pin | Function |
| :--- | :--- | :--- |
| `2` | `STEP` | Step pulse input |
| `3` | `DIR` | Direction input |
| `4` | `EN` | Enable input (Active LOW) |
| `5` | `MS1` | Microstepping configuration 1 |
| `6` | `MS2` | Microstepping configuration 2 |

*Note: Ensure your stepper motor power supply is properly connected to the driver (VMOT/GND) and the logic power (VDD/GND) is connected to the Arduino's 5V or 3.3V (depending on your logic level).*

## Code Configuration

At the top of `src/main.cpp`, you will find a user configuration block:

```cpp
// ==========================================
// USER CONFIGURATION
// ==========================================
const int MICROSTEPS = 8; 
float currentMaxSpeed = 1000.0;
float currentAcceleration = 500.0;
// ==========================================
```

*   `MICROSTEPS`: Sets the hardware microstepping resolution. Valid options for the TMC2209 in standalone mode are `8`, `16`, `32`, or `64`.
*   `currentMaxSpeed`: The default maximum speed in steps per second.
*   `currentAcceleration`: The default acceleration in steps per second squared.

## Usage (Serial Commands)

1.  Upload the code to your Arduino.
2.  Open the Arduino Serial Monitor.
3.  Set the baud rate to **9600**.
4.  Ensure the line ending is set to "Newline" or "Both NL & CR".

You will see a startup menu. You can control the motor by sending the following commands:

*   **`M<number>` (Move):** Moves the motor a relative number of steps. 
    *   *Example:* `M800` moves 800 steps forward.
    *   *Example:* `M-800` moves 800 steps backward.
*   **`V<number>` (Velocity/Speed):** Sets the new maximum speed in steps per second.
    *   *Example:* `V1500`
*   **`A<number>` (Acceleration):** Sets the new acceleration in steps per second squared.
    *   *Example:* `A500`
*   **`S` (Stop):** Immediately stops the motor. It calculates the necessary target position to stop with the current deceleration curve.
*   **`?` (Help):** Prints the command menu and displays the currently configured speed and acceleration values.
