# Four-Pump Gravimetric Dispensing Controller

This project controls a four-pump liquid dispensing system on an Arduino Giga R1 WiFi. Each pump uses a TMC2209 stepper driver with a dedicated step pin and shared direction, enable, and microstepping pins.

The firmware dispenses by mass using live scale feedback over `Serial1`. It runs each pump sequentially, waits for the scale to settle between pumps, and supports calibration plus low/high viscosity profiles.

## Hardware

| Arduino Giga Pin | Function |
| :--- | :--- |
| `53` | Pump 1 `STEP` |
| `51` | Pump 2 `STEP` |
| `49` | Pump 3 `STEP` |
| `47` | Pump 4 `STEP` |
| `45` | Pump 5 `STEP` |
| `43` | Pump 6 `STEP` |
| `41` | Pump 7 `STEP` |
| `27` | Shared `DIR` |
| `29` | Shared `EN`, active low |
| `25` | Shared `MS1` |
| `23` | Shared `MS2` |
| `Serial1` | Scale serial input at 9600 baud |
| USB `Serial` | Host/dashboard command interface at 9600 baud |

The Arduino Giga uses 3.3 V logic. Keep TMC2209 logic power on 3.3 V, not 5 V.

## Build And Upload

The root `platformio.ini` targets the Arduino Giga R1 WiFi M7 core:

```ini
[env:giga_r1_m7]
platform = ststm32
board = giga_r1_m7
framework = arduino
monitor_speed = 9600
```

Useful commands:

```powershell
C:\Users\littl\.platformio\penv\Scripts\platformio.exe run
C:\Users\littl\.platformio\penv\Scripts\platformio.exe run --target upload
C:\Users\littl\.platformio\penv\Scripts\platformio.exe device monitor
```

## Serial Commands

Targets are entered in pump order. Send one numeric value for each pump:

```text
10.00
5.00
0.00
2.50
```

Values greater than `1.50` g run that pump. `0.00` skips that pump.

Other commands:

| Command | Action |
| :--- | :--- |
| `H1` through `H4` | Set pump viscosity profile to high, for glycerol |
| `L1` through `L4` | Set pump viscosity profile to low, for water |
| `C1` through `C4` | Calibrate the selected pump profile |
| `T` | Software tare the scale |
| `S` | Emergency stop |

## Dashboard

Open `dashboard/index.html` in a browser that supports Web Serial, such as Chrome or Edge. The dashboard can run in simulator mode or connect to the Arduino Giga over USB at 9600 baud.
