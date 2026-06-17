import serial
import time
import sys

def main():
    port = "/dev/cu.usbmodem1101"
    baud = 115200
    
    print(f"Connecting to {port} at {baud} baud...")
    ser = None
    for attempt in range(5):
        try:
            ser = serial.Serial(port, baud, timeout=0.5)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.5)
            ser.dtr = True
            ser.rts = True
            time.sleep(3.0)  # Wait for Arduino boot
            break
        except Exception as e:
            print(f"Attempt {attempt+1} failed: {e}")
            time.sleep(1.0)
            
    if not ser:
        print("Could not connect to serial port.")
        sys.exit(1)
        
    print("Serial port opened. Reading startup messages...")
    
    start_read = time.time()
    while time.time() - start_read < 2.0:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            print(f"[Init] {line}")
            
    try:
        # Step 1: Set a safe acceleration first
        print("\n>>> Setting acceleration to 400 steps/sec^2 <<<")
        ser.write(b"ACCEL 400\n")
        ser.flush()
        time.sleep(0.5)
        
        # Step 2: Trigger silent automatic ramping test
        print("\n>>> Sending TEST command (Automatic Silent Ramp: 100 -> 500 RPM) <<<")
        ser.write(b"TEST\n")
        ser.flush()
        
        # Read the immediate acknowledgment printed by main.cpp
        time.sleep(0.5)
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial Out] {line}")
                
        # Close the port immediately so that USB lines are completely idle/de-energized on host side
        print("\n>>> Closing serial port on host PC to guarantee zero USB traffic during test... <<<")
        ser.close()
        
        print("Waiting 60 seconds for the auto-ramping test to finish on the Giga board...")
        time.sleep(60.0)
        
        # Reopen the port to read the stop message
        print("\n>>> Reconnecting to serial port to read final status... <<<")
        ser = serial.Serial(port, baud, timeout=1.0)
        time.sleep(1.0)
        
        # Check if there are any messages waiting (like "Mixer: Fully stopped...")
        print("Reading final status messages...")
        start_final = time.time()
        while time.time() - start_final < 4.0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial Out] {line}")
                
    except Exception as e:
        print(f"\nError occurred: {e}")
    finally:
        if ser and ser.is_open:
            ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
