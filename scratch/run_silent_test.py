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
        
        # Read the initial handshake messages
        print("Reading handshake response...")
        start_handshake = time.time()
        while time.time() - start_handshake < 4.0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial Out] {line}")
                
        # Now go completely quiet for 60 seconds while the board executes the test
        print("\n>>> Going SILENT on USB serial lines for 60 seconds... <<<")
        print("The motor is accelerating from 100 to 500 RPM, running at 500 RPM, and then stopping.")
        time.sleep(60.0)
        
        # Read the stop/completion message
        print("\n>>> Waking up! Reading final messages from the board... <<<")
        start_final = time.time()
        while time.time() - start_final < 5.0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial Out] {line}")
                
    except KeyboardInterrupt:
        print("\nInterrupted. Sending STOP...")
        try:
            ser.write(b"STOP\n")
            ser.flush()
        except:
            pass
    except Exception as e:
        print(f"\nError occurred: {e}")
    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
