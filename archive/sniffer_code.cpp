#include <Arduino.h>

// ============================================================
// USS-DBS Scale Sniffer
// Reads continuous weight data from a digital scale over Serial1
// and prints parsed values to USB Serial for debugging/inspection.
// ============================================================

// --- Configuration ---
const long SCALE_BAUD = 9600;   // Baud rate for scale serial port
const long USB_BAUD   = 9600;   // Baud rate for USB serial output

// --- Global Buffers ---
String scaleBuffer = "";                    // Accumulates raw bytes from scale
const unsigned int MAX_BUFFER_LENGTH = 32;  // Max bytes per scale packet

// ============================================================
// SECTION 1: Scale Data Parsing
// ============================================================

/**
 * @brief Extracts a numeric weight value from a raw scale string.
 *
 * Scale output looks like: "+  123.45 g" or "-   0.50 g"
 * This function strips spaces, the '+'/'-' sign prefix, and the "g" unit,
 * leaving only digits, dots, and minus signs for conversion to float.
 *
 * @param raw Raw string received from the scale (e.g. "+  123.45 g")
 */
void processScaleData(String raw) {
  String cleanStr = "";

  for (unsigned int i = 0; i < raw.length(); i++) {
    char ch = raw.charAt(i);
    if (isDigit(ch) || ch == '.' || ch == '-') {
      cleanStr += ch;
    }
  }

  if (cleanStr.length() > 0) {
    float weight = cleanStr.toFloat();

    Serial.print("Raw: [");
    Serial.print(raw);
    Serial.print("] -> Parsed: ");
    Serial.print(weight, 2);
    Serial.println(" g");
  }
}

// ============================================================
// SECTION 2: Initialization
// ============================================================

/**
 * @brief Sets up USB and hardware serial ports for scale communication.
 *
 * USB Serial (Serial) is used for outputting parsed data to the host PC.
 * Hardware Serial (Serial1, pins 0/1) receives raw data from the scale.
 */
void setup() {
  Serial.begin(USB_BAUD);
  Serial1.begin(SCALE_BAUD);

  Serial.println("========================================");
  Serial.println("USS-DBS Scale Sniffer Initialized");
  Serial.println("Ensure Scale is set to 'C5-0' (Continuous)");
  Serial.println("========================================");
}

// ============================================================
// SECTION 3: Main Loop — Read, Parse, Output
// ============================================================

/**
 * @brief Continuously reads scale data and parses weight values.
 *
 * Flow:
 *   1. Read bytes from Serial1 (scale) into a buffer until \r or \n.
 *   2. On line ending, pass the buffer to processScaleData() and clear it.
 *   3. Every 2 seconds, check for scale connectivity (no-op heartbeat).
 */
void loop() {
  // Read scale serial data and accumulate into buffer
  while (Serial1.available() > 0) {
    char c = Serial1.read();

    // Packet delimiter — process the accumulated buffer
    if (c == '\n' || c == '\r') {
      if (scaleBuffer.length() > 0) {
        processScaleData(scaleBuffer);
        scaleBuffer = "";
      }
    } else {
      if (scaleBuffer.length() < MAX_BUFFER_LENGTH) {
        scaleBuffer += c;
      }
    }
  }

  // Heartbeat: silent check for scale connectivity every 2 seconds
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 2000) {
    if (Serial1.available() == 0 && scaleBuffer.length() == 0) {
      // No data received — scale may be disconnected
    }
    lastCheck = millis();
  }
}
