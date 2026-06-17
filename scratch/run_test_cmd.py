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
        # Optionally, set a lower acceleration first for safety
        print("\n>>> Setting acceleration to 500 steps/sec^2 (gentle ramp) <<<")
        ser.write(b"ACCEL 500\n")
        ser.flush()
        time.sleep(0.5)
        
        print("\n>>> Sending TEST command <<<")
        ser.write(b"TEST\n")
        ser.flush()
        
        # Monitor the ramping test (takes ~50 seconds to complete)
        print("Monitoring test progress for 65 seconds...")
        start_monitor = time.time()
        while time.time() - start_monitor < 65.0:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[Serial Out] {line}")
            except serial.SerialException as se:
                print(f"Serial read error: {se}")
                raise se
                
        print("\nSending STOP command...")
        ser.write(b"STOP\n")
        ser.flush()
        
        # Read final stop messages
        time.sleep(2)
        while ser.in_waiting:
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
