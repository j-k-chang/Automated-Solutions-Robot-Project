# Robot Mixer Controller (Arduino Giga & BTT TMC2209)

This directory contains a modular, non-blocking controller for a liquid mixer extension. The system is designed to run on the **Arduino Giga R1 WiFi (M7 Core)**, driving a **NEMA 17 stepper motor** using a **BigTreeTech (BTT) TMC2209 stepper driver** in standalone mode.

---

## ⚠️ Critical Safety Warning: Logic Levels

> [!CAUTION]
> **ARDUINO GIGA RUNS ON 3.3V LOGIC ONLY!**
> * The Arduino Giga is **NOT 5V tolerant** on its input/output pins.
> * You **MUST** connect the **VIO** (Logic Power Supply) pin of your BTT TMC2209 driver to the **3.3V Pin** of the Arduino Giga.
> * **DO NOT** connect VIO to the 5V pin, as this will damage the Giga's STM32 microcontroller.

---

## Hardware Pin Mapping & Wiring Guide

Connect your components according to the table below. Make sure all power is off before wiring.

### 1. Arduino Giga to BTT TMC2209 Wiring

| Arduino Giga Pin | TMC2209 Driver Pin | Function / Description |
| :--- | :--- | :--- |
| **`52`** | `STEP` | Step pulse input (creates rotation) |
| **`48`** | `DIR` | Direction input (determines direction) |
| **`50`** | `EN` | Enable input (Active **LOW**. Disables motor to keep cool when idle) |
| **`3.3V`** | `VIO` | Logic Power supply (Uses 3.3V Logic) |
| **`GND`** | `GND` | Logic Ground reference |

### 2. Hardware Microstepping Setup (MS1 and MS2 Pins)

Since the mixing process runs at a high speed, we set the microstepping hardware-level resolution to **1/8 microstepping** (which provides high torque at high RPMs).

* **Wiring**: Wire/bridge both **MS1** and **MS2** pins on the TMC2209 driver physically to the **GND** rail.
* This locks the driver into **1/8 microstepping mode** permanently without using any extra microcontroller pins. The TMC2209's microPlyer interpolator will automatically interpolate this to 256 microsteps internally for silent operation.

### 3. Stepper Motor & High Voltage Connections

| TMC2209 Pin | Connection Destination | Notes |
| :--- | :--- | :--- |
| `VM` | Motor Power Supply Positive (**12V - 24V DC**) | Put a **100µF capacitor** close to the driver between VM and GND to absorb voltage spikes! |
| `GND` | Motor Power Supply Ground | Common ground with power supply. |
| `A1, A2, B1, B2` | NEMA 17 Stepper Motor coils | Wire the two phase coils of your NEMA 17 here. |

---

## Serial Command Interface Reference

Connect your Arduino Giga to your computer using a USB-C cable. Open your preferred serial monitor (such as VS Code Serial Monitor or Arduino Serial Monitor), set the baud rate to **115200**, and ensure you are sending **both Newline & Carriage Return (NL & CR)**.

You will be greeted with the command interface. Send any of the following commands:

| Command | Action | Description |
| :--- | :--- | :--- |
| **`START`** | Start Continuous Mixing | Motor smoothly accelerates over 7 seconds to the standard liquid-mixing speed (200 RPM) and spins continuously. |
| **`STOP`** | Decelerate & Stop | Gracefully ramps speed down to zero. Once stopped, it **disables the stepper driver coils** to prevent heat build-up. |

---

## Operating Instructions & Best Practices

1. **Keep the Driver Cool**: When the motor is idle, the firmware automatically disables the TMC2209 driver (setting the `EN` pin HIGH), which stops current flowing to the motor coils and lets the system run completely cool.
2. **Smooth Ramping**: The motor uses AccelStepper to accelerate and decelerate gradually rather than stopping instantly. This prevents the liquid from spilling or splashing when starting or stopping the mixer.
