#ifndef MULTIPUMP_DISPENSING_H
#define MULTIPUMP_DISPENSING_H

#include <Arduino.h>
#include <AccelStepper.h>
#include "config.h"

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

// --- DC Fan Relay Pin ---
const int FAN_PIN    = 22;  ///< Relay control pin for the DC fan

// --- Communication Settings ---
const long SCALE_BAUD = 9600; ///< Baud rate for serial communication with the digital scale (Serial1)
const long USB_BAUD   = 9600; ///< Baud rate for USB serial communication with the host PC (Serial)

// --- Dispensing Configuration ---
extern const float BULK_SPEED;     ///< High speed for volumetric bulk fill
extern const float TRICKLE_SPEED;  ///< Speed for fine micro-pulsing

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
  SEQ_MIXING_BETWEEN,   ///< Run mixer between pump cycles
  SEQ_SETTLE_AFTER_MIX, ///< Wait for liquid to settle after mixer stops
  SEQ_FINAL_MIXING,     ///< Run mixer at the end of the recipe
  SEQ_DONE,
  SEQ_CALIBRATE_RUN,
  SEQ_CALIBRATE_RETRACT,
  SEQ_CALIBRATE_WAIT_INPUT,
  SEQ_DROP_CAL,
  SEQ_MAINTENANCE_RUN   ///< Active priming, purging, or flushing sequence
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
