#include <Arduino.h>
#include "Mixer.h"

// --- Global Mixer Instance ---
// Standard pins: STEP = 10, DIR = 11, EN = 12
Mixer mixer(10, 11, 12);

// Serial command parsing buffers
String inputBuffer = "";
const unsigned int MAX_BUF = 60;

void printMenu();
void handleCommands();

void setup() {
    // Wait for USB serial initialization
    delay(1500);
    Serial.begin(115200);

    // Initialize Mixer driver
    mixer.begin();

    // Print welcome interface
    printMenu();
}

void loop() {
    // Always call the mixer update function to execute step timing
    mixer.update();

    // Handle serial inputs
    handleCommands();
}

void printMenu() {
    Serial.println("\n==================================================");
    Serial.println("         ROBOT MIXER CONTROLLER V1.0              ");
    Serial.println("==================================================");
    Serial.println("COMMANDS:");
    Serial.println("  START - Start mixing (with smooth 4s ramp-up)");
    Serial.println("  STOP  - Stop mixing  (with smooth 4s ramp-down)");
    Serial.println("==================================================");
    Serial.print("mixer> ");
}

void handleCommands() {
    while (Serial.available() > 0) {
        char c = Serial.read();

        // Check for carriage return or newline
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                inputBuffer.trim();

                if (inputBuffer.equalsIgnoreCase("START")) {
                    mixer.startContinuous();
                }
                else if (inputBuffer.equalsIgnoreCase("STOP")) {
                    mixer.stop();
                }
                else {
                    Serial.println("Invalid command. Type START or STOP.");
                }

                Serial.print("\nmixer> ");
                inputBuffer = ""; // Reset buffer
            }
        }
        // Accumulate characters (up to limit to prevent overflow)
        else if (inputBuffer.length() < MAX_BUF) {
            inputBuffer += c;
        }
    }
}
