#ifndef MULTIPUMP_DISPENSING_H
#define MULTIPUMP_DISPENSING_H

#include <Arduino.h>
#include <AccelStepper.h>

/**
 * @namespace MultiPump
 * @brief Encapsulates all global variables, constants, and functions required for 
 *        the multi-pump gravimetric dispensing system to prevent naming collisions.
 */
namespace MultiPump {

// --- Shared Global Pins (Parallel Wiring) ---
const int SHARED_DIR = 27;  ///< Shared Direction pin for both stepper drivers
const int SHARED_EN  = 29;  ///< Shared Enable pin for both stepper drivers (active LOW)
const int SHARED_MS1 = 25;  ///< Shared Microstepping configuration pin 1
const int SHARED_MS2 = 23;  ///< Shared Microstepping configuration pin 2

// --- Pump Specific STEP Pins ---
const int PUMP1_STEP = 53;  ///< Separate Step pin for Pump 1 stepper motor driver
const int PUMP2_STEP = 51;  ///< Separate Step pin for Pump 2 stepper motor driver
const int PUMP3_STEP = 49;  ///< Separate Step pin for Pump 3 stepper motor driver
const int PUMP4_STEP = 47;  ///< Separate Step pin for Pump 4 stepper motor driver
const int PUMP5_STEP = 45;  ///< Separate Step pin for Pump 5 stepper motor driver
const int PUMP6_STEP = 43;  ///< Separate Step pin for Pump 6 stepper motor driver
const int PUMP7_STEP = 41;  ///< Separate Step pin for Pump 7 stepper motor driver
const int FAN_PIN    = 22;  ///< Relay control pin for the DC fan

const int PUMP_COUNT = 7;   ///< Total number of pumps on the shared bus

// --- Communication Settings ---
const long SCALE_BAUD = 9600; ///< Baud rate for serial communication with the digital scale (Serial1)
const long USB_BAUD   = 9600; ///< Baud rate for USB serial communication with the host PC (Serial)

// --- Dispensing Configuration ---
extern const float BULK_SPEED;     ///< High speed for volumetric bulk fill
extern const float TRICKLE_SPEED;  ///< Speed for fine micro-pulsing
extern const float CALIBRATE_SPEED;///< Speed during calibration run

// --- Retraction Constants ---
extern const long retractStepsWater;    ///< Retraction steps for Water (1/64)
extern const long retractStepsGlycerol; ///< Retraction steps for Glycerol (1/16)

/**
 * @enum DispenseState
 * @brief Sub-states for a single pump's adaptive progressive approximation cycle.
 */
enum DispenseState {
  DISPENSE_IDLE,
  DISPENSE_BULK_FILL,
  DISPENSE_SETTLE_BULK,
  DISPENSE_TRIM_PULSE,
  DISPENSE_SETTLE_TRIM,
  DISPENSE_SUCK_BACK,
  DISPENSE_PRESSURE_RELIEF_FAST,
  DISPENSE_PRESSURE_RELIEF_FINISH,
  DISPENSE_RETRACTING,
  DISPENSE_COMPLETE,
  DISPENSE_ERROR
};

/**
 * @enum SequenceState
 * @brief Overarching state machine for sequential multi-pump dispensing and calibration.
 */
enum SequenceState {
  SEQ_PROMPT_TARGET,
  SEQ_DISPENSE_ACTIVE,
  SEQ_SETTLE_BETWEEN,   ///< Wait for scale to settle between pump cycles
  SEQ_DONE,
  SEQ_CALIBRATE_RUN,
  SEQ_CALIBRATE_WAIT_INPUT
};

/**
 * @brief Initializes the multi-pump system. 
 *        Configures pins, serial ports, and stepper motor parameters. 
 *        Must be called once in the Arduino setup() function.
 */
void multipumpSetup();

/**
 * @brief Main execution loop for the multi-pump system. 
 *        Handles serial communication, state transitions, and motor control.
 *        Must be called continuously in the Arduino loop() function.
 */
void multipumpLoop();

} // namespace MultiPump

#endif
