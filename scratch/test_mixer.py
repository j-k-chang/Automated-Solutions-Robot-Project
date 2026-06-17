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
            # Toggle DTR/RTS to reset the board if supported
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
            
    # Test sequence: RPM levels to test
    rpm_levels = [100, 200, 300, 350, 400, 420, 450, 500]
    
    print("\nStarting Test Sequence...")
    
    # Send START first (default is 400 RPM, but we'll override it immediately)
    print("Sending command: START")
    ser.write(b"START\n")
    ser.flush()
    time.sleep(0.5)
    
    try:
        for rpm in rpm_levels:
            print(f"\n>>> Setting Target RPM to: {rpm} <<<")
            ser.write(f"RPM {rpm}\n".encode())
            ser.flush()
            
            # Monitor telemetry at this speed for 4 seconds
            start_step = time.time()
            while time.time() - start_step < 4.0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[Serial Out] {line}")
                except serial.SerialException as se:
                    print(f"Serial read error: {se}")
                    raise se
                    
        print("\nTest sequence complete. Ramping down to STOP...")
        ser.write(b"STOP\n")
        ser.flush()
        
        # Monitor stop ramp
        start_stop = time.time()
        while time.time() - start_stop < 6.0:
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
        print(f"\nError occurred during test: {e}")
    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
