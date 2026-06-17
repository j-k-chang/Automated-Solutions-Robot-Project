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
    
    # Read whatever is available in the first 2 seconds
    start_read = time.time()
    while time.time() - start_read < 2.0:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            print(f"[Init] {line}")
            
    try:
        # Step 1: Set lower acceleration for stability
        print("\n>>> Setting acceleration to 500 steps/sec^2 (gentle ramp) <<<")
        ser.write(b"ACCEL 500\n")
        ser.flush()
        time.sleep(0.5)
        
        # Step 2: Set safe starting RPM
        print("\n>>> Setting initial target to 150 RPM <<<")
        ser.write(b"RPM 150\n")
        ser.flush()
        time.sleep(0.5)
        
        # Step 3: Start the mixer
        print("\n>>> Sending START command <<<")
        ser.write(b"START\n")
        ser.flush()
        time.sleep(0.5)
        
        # Incremental speeds to test
        rpm_steps = [150, 200, 250, 300, 350, 400, 450, 500]
        
        for rpm in rpm_steps:
            print(f"\n>>> Advancing Target RPM to: {rpm} <<<")
            ser.write(f"RPM {rpm}\n".encode())
            ser.flush()
            
            # Monitor telemetry for 5 seconds at this speed
            start_step = time.time()
            while time.time() - start_step < 5.0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[Serial Out] {line}")
                except serial.SerialException as se:
                    print(f"Serial read error: {se}")
                    raise se
                    
        print("\nRamping down to STOP...")
        ser.write(b"STOP\n")
        ser.flush()
        
        # Monitor stop ramp
        start_stop = time.time()
        while time.time() - start_stop < 10.0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial Out] {line}")
                
    except KeyboardInterrupt:
        print("\nInterrupted by user. Sending STOP...")
        try:
            ser.write(b"STOP\n")
            ser.flush()
        except:
            pass
    except Exception as e:
        print(f"\nError occurred during test: {e}")
    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
