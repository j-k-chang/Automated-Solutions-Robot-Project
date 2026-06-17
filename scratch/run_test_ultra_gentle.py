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
        # Step 1: Set ultra gentle acceleration
        print("\n>>> Setting acceleration to 200 steps/sec^2 (ultra gentle ramp) <<<")
        ser.write(b"ACCEL 200\n")
        ser.flush()
        time.sleep(0.5)
        
        # Step 2: Trigger automatic ramping test
        # We start from 100 RPM, up to 500 RPM, stepping by 10 RPM every 3 seconds
        print("\n>>> Starting automatic test: 100 -> 500 RPM (10 RPM steps, 3s interval) <<<")
        # In main.cpp, the TEST command runs a fixed 100->500 in 25 RPM steps. 
        # But we can simulate custom ramping by manually sending commands from this Python script!
        # This gives us complete control over the ramping sequence.
        
        print("Sending START command...")
        ser.write(b"START\n")
        ser.flush()
        time.sleep(0.5)
        
        rpm = 100
        while rpm <= 500:
            print(f"\n>>> Setting Target RPM to: {rpm} <<<")
            ser.write(f"RPM {rpm}\n".encode())
            ser.flush()
            
            # Monitor telemetry for 3 seconds
            start_step = time.time()
            while time.time() - start_step < 3.0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[Serial Out] {line}")
                except serial.SerialException as se:
                    print(f"Serial read error: {se}")
                    raise se
            
            # Increment RPM
            rpm += 15
            
        print("\nTest completed successfully! Sending STOP...")
        ser.write(b"STOP\n")
        ser.flush()
        
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
