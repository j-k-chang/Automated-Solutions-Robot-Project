#include <Arduino.h>
#include "Mixer.h"

// --- Global Mixer Instance ---
// Standard pins: STEP = 52, DIR = 48, EN = 50
Mixer mixer(52, 48, 50);

// Serial command parsing buffers
String inputBuffer = "";
const unsigned int MAX_BUF = 60;

void printMenu();
void handleCommands();

void setup() {
    // Wait for USB serial initialization
    delay(1500);
    Serial.begin(115200);

    // Initialize all hardware serial ports for loopback/diagnostics
    Serial1.begin(115200);
    Serial2.begin(115200);
    Serial3.begin(115200);
    Serial4.begin(115200);

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
    Serial.println("         ROBOT MIXER CONTROLLER V1.3              ");
    Serial.println("==================================================");
    Serial.println("COMMANDS:");
    Serial.println("  START       - Start mixing (ramps up to target RPM)");
    Serial.println("  STOP        - Stop mixing (graceful ramp down)");
    Serial.println("  TEST        - Start auto-ramping test (50 -> 450 RPM)");
    Serial.println("  RPM <val>   - Set target RPM (e.g. RPM 300, max 400)");
    Serial.println("  ACCEL <val> - Set acceleration (steps/s^2, default 1523.81)");
    Serial.println("  STATUS      - Show current mixer settings and state");
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

                String upperInput = inputBuffer;
                upperInput.toUpperCase();

                if (upperInput.equals("START")) {
                    mixer.startContinuous();
                }
                else if (upperInput.equals("STOP")) {
                    mixer.stop();
                }
                else if (upperInput.equals("TEST")) {
                    // Starts from 50 RPM to 400 RPM in 50 RPM steps, waiting 3 seconds per step
                    mixer.startAutoRampTest(50.0f, 400.0f, 50.0f, 3000);
                }
                else if (upperInput.startsWith("RPM ")) {
                    float rpm = inputBuffer.substring(4).toFloat();
                    mixer.setTargetRPM(rpm);
                    Serial.print("Target RPM set to: ");
                    Serial.println(mixer.getTargetRPM());
                }
                else if (upperInput.startsWith("ACCEL ")) {
                    float accel = inputBuffer.substring(6).toFloat();
                    mixer.setAcceleration(accel);
                    Serial.print("Acceleration set to: ");
                    Serial.print(mixer.getAcceleration());
                    Serial.println(" steps/sec^2");
                }
                else if (upperInput.equals("STATUS")) {
                    Serial.println("\n--- MIXER STATUS ---");
                    Serial.print("State:        ");
                    Serial.println(mixer.getStateString());
                    Serial.print("Target Speed: ");
                    Serial.print(mixer.getTargetRPM());
                    Serial.println(" RPM");
                    Serial.print("Acceleration: ");
                    Serial.print(mixer.getAcceleration());
                    Serial.println(" steps/sec^2");
                    Serial.print("Auto-Ramping: ");
                    Serial.println(mixer.isAutoRamping() ? "Active" : "Inactive");
                    
                    // UART status & driver diagnostics
                    bool uartConnected = mixer.checkUARTConnection();
                    Serial.print("Driver UART:  ");
                    if (uartConnected) {
                        Serial.println("ONLINE (SpreadCycle Active)");
                    } else {
                        Serial.println("OFFLINE (StealthChop Standalone Mode)");
                    }
                    
                    // Hardware loopback tests
                    struct SerialTest {
                        const char* name;
                        HardwareSerial* port;
                        const char* pins;
                    } tests[] = {
                        {"Serial1", &Serial1, "0/1"},
                        {"Serial2", &Serial2, "19/18 (RX1/TX1)"},
                        {"Serial3", &Serial3, "17/16 (RX2/TX2)"},
                        {"Serial4", &Serial4, "15/14 (RX3/TX3)"}
                    };
                    for (auto& t : tests) {
                        while (t.port->available() > 0) t.port->read();
                        t.port->write(0xAA);
                        delay(5);
                        Serial.print(t.name);
                        Serial.print(" (pins ");
                        Serial.print(t.pins);
                        Serial.print(") Loopback: ");
                        if (t.port->available() > 0) {
                            int val = t.port->read();
                            if (val == 0xAA) {
                                Serial.println("PASSED");
                            } else {
                                Serial.print("FAILED (Read wrong byte: 0x");
                                Serial.print(val, HEX);
                                Serial.println(")");
                            }
                            while (t.port->available() > 0) t.port->read();
                        } else {
                            Serial.println("FAILED (No echo)");
                        }
                    }

                    Serial.print("Raw GCONF:    0x");
                    Serial.println(mixer.getGCONF(), HEX);
                    Serial.print("Raw IOIN:     0x");
                    Serial.println(mixer.getIOIN(), HEX);
                    if (uartConnected) {
                        Serial.print("Driver Current: ");
                        Serial.print(mixer.getDriverCurrent());
                        Serial.println(" mA RMS");
                        Serial.print("Driver Microsteps: ");
                        Serial.println(mixer.getDriverMicrosteps());
                    }
                    Serial.println("--------------------");
                }
                else {
                    Serial.println("Invalid command. Type START, STOP, TEST, RPM <val>, ACCEL <val>, or STATUS.");
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
