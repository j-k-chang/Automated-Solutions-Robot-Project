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

// --- Pump 1 Pins (same as existing main.cpp) ---
const int PUMP1_STEP = 2;   ///< Step pin for Pump 1 stepper motor driver
const int PUMP1_DIR  = 3;   ///< Direction pin for Pump 1 stepper motor driver
const int PUMP1_EN   = 4;   ///< Enable pin for Pump 1 stepper motor driver (active LOW)
const int PUMP1_MS1  = 5;   ///< Microstepping configuration pin 1 for Pump 1
const int PUMP1_MS2  = 6;   ///< Microstepping configuration pin 2 for Pump 1

// --- Pump 2 Pins (new) ---
const int PUMP2_STEP = 7;   ///< Step pin for Pump 2 stepper motor driver
const int PUMP2_DIR  = 8;   ///< Direction pin for Pump 2 stepper motor driver
const int PUMP2_EN   = 9;   ///< Enable pin for Pump 2 stepper motor driver (active LOW)
const int PUMP2_MS1  = 10;  ///< Microstepping configuration pin 1 for Pump 2
const int PUMP2_MS2  = 11;  ///< Microstepping configuration pin 2 for Pump 2

// --- Communication Settings ---
const long SCALE_BAUD = 9600; ///< Baud rate for serial communication with the digital scale (Serial1)
const long USB_BAUD   = 9600; ///< Baud rate for USB serial communication with the host PC (Serial)

// --- Dispensing Configuration ---
extern const float initialSpeed;  ///< Starting speed for the pump during the FAST_FILL phase (steps/sec)
extern const float approachSpeed; ///< Reduced speed used when approaching the target weight (steps/sec)
extern const float trickleSpeed;  ///< Very slow speed used for the final drop-by-drop dispensing (steps/sec)
extern const long  suckBackSteps; ///< Number of steps to reverse the pump at the end to prevent dripping
extern const float suckBackSpeed; ///< Speed of the pump during the suck-back operation (steps/sec)

// --- Precision Tuning ---
extern const float stopOffset;             ///< Early stopping offset (in grams) to account for fluid in transit before settling
extern const unsigned long settleTimeLimit;///< Time (in milliseconds) to wait for the scale to stabilize after stopping the pump

/**
 * @enum DispenseState
 * @brief Represents the internal state machine for a single pump's dispensing cycle.
 */
enum DispenseState { 
  FAST_FILL,   ///< Bulk dispensing at high speed
  APPROACH,    ///< Slower dispensing as the target weight nears
  TRICKLE,     ///< Very slow, precise dispensing for the final adjustments
  SETTLE,      ///< Pump stopped; waiting for the scale reading to stabilize
  SUCK_BACK,   ///< Reversing the pump slightly to prevent fluid from dripping
  ERROR_STATE  ///< Error condition (e.g., scale timeout or emergency stop)
};

/**
 * @enum SequenceState
 * @brief Represents the overarching state machine for the sequential multi-pump process.
 */
enum SequenceState { 
  SEQ_PROMPT_PUMP1,   ///< Waiting for user to input target weight for Pump 1
  SEQ_DISPENSE_PUMP1, ///< Actively running the dispensing cycle for Pump 1
  SEQ_PROMPT_PUMP2,   ///< Waiting for user to input target weight for Pump 2
  SEQ_DISPENSE_PUMP2, ///< Actively running the dispensing cycle for Pump 2
  SEQ_DONE            ///< Entire multi-pump sequence completed successfully
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
