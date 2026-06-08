import serial
import time
import csv
import sys
import re

# Sweep targets from 10g to 100g for accuracy check
TEST_TARGETS = [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0]
DUMMY_TARGETS = [0.0, 0.0, 0.0]  # Pump 2-4 targets, 0.0g skips each pump

def find_serial_ports():
    import serial.tools.list_ports
    ports = list(serial.tools.list_ports.comports())
    return [p.device for p in ports]

def run_test():
    print("==================================================")
    print("Multi-Pump Accuracy Test Sweep (Pump 1)")
    print("==================================================")
    
    ports = find_serial_ports()
    if not ports:
        print("No active COM ports found. Please connect your Arduino Board.")
        sys.exit(1)
        
    print("Available COM Ports:")
    for idx, p in enumerate(ports):
        print(f"[{idx}] {p}")
        
    port_idx = input("Select Port index (default 0): ").strip()
    port = ports[int(port_idx)] if port_idx else ports[0]
    
    print(f"\nConnecting to {port} at 9600 Baud...")
    try:
        ser = serial.Serial(port, 9600, timeout=1.0)
        time.sleep(3)  # Wait for Arduino boot and reset
    except Exception as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)
        
    print("\nStarting test sweep...")
    results = []
    
    # Read any leftover buffer data
    ser.reset_input_buffer()
    
    # Wait for the first telemetry packet to ensure communication is online
    print("Waiting for serial telemetry handshake...")
    start_time = time.time()
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if "TELEMETRY:" in line:
            print(f"Handshake OK: {line}")
            break
        if time.time() - start_time > 5.0:
            print("Warning: Telemetry handshake timed out, trying to proceed anyway...")
            break
            
    for target in TEST_TARGETS:
        print(f"\n--- Testing Target: {target}g ---")
        
        # Step 1: Send Target for Pump 1
        print(f"Sending Pump 1 target weight: {target}g")
        ser.write(f"{target}\n".encode())
        
        # Wait for Pump 1 target configuration confirmation
        print("Waiting for Pump 1 target confirmation...")
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial] {line}")
            if "Pump 1 Target set to:" in line:
                break
                
        # Step 2: Send Target for Pumps 2-4
        for offset, dummy_target in enumerate(DUMMY_TARGETS, start=2):
            time.sleep(0.5)
            print(f"Sending Pump {offset} dummy target weight: {dummy_target}g")
            ser.write(f"{dummy_target}\n".encode())

            print(f"Waiting for Pump {offset} target confirmation...")
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[Serial] {line}")
                if f"Pump {offset} Target set to:" in line:
                    break

        # Step 3: Monitor Pump 1 dispense and parse final delivered amount
        actual_dispensed = None
        print("Pump 1 dispensing... monitoring output...")
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial] {line}")
            if "Pump 1 done. Dispensed:" in line:
                match = re.search(r"Dispensed:\s*([\d\.]+)", line)
                if match:
                    actual_dispensed = float(match.group(1))
                    break
            if "!!! ERROR: SCALE TIMEOUT WATCHDOG !!!" in line:
                print("Error: Scale timeout watchdog triggered! Aborting test run.")
                ser.close()
                sys.exit(1)
                
        # Step 4: Wait for the whole sequence to finish
        print("Waiting for sequence completion...")
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[Serial] {line}")
            if "Entire multi-pump dispensing sequence completed successfully!" in line:
                break
                
        # Calculate error stats
        error = actual_dispensed - target
        pct_error = (error / target) * 100.0
        results.append({
            'Target': target,
            'Actual': actual_dispensed,
            'Error': error,
            'Error_Pct': pct_error
        })
        print(f"Result -> Target: {target}g | Actual: {actual_dispensed}g | Error: {error:+.3f}g ({pct_error:+.2f}%)")
        time.sleep(2) # Brief cooldown settle before next cycle
        
    ser.close()
    
    # Save to CSV
    csv_file = "accuracy_results.csv"
    try:
        with open(csv_file, mode='w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['Target (g)', 'Actual (g)', 'Error (g)', 'Error (%)'])
            for r in results:
                writer.writerow([r['Target'], r['Actual'], f"{r['Error']:.3f}", f"{r['Error_Pct']:.2f}"])
        print(f"\nResults successfully saved to CSV: {csv_file}")
    except Exception as e:
        print(f"Error saving CSV file: {e}")
            
    # Generate accuracy plot
    generate_plot(results)
            
    print("\n==================================================")
    print("Test Sweep Complete!")
    print("==================================================")
    print(f"{'Target (g)':<12}{'Actual (g)':<12}{'Error (g)':<12}{'Error (%)':<12}")
    for r in results:
        print(f"{r['Target']:<12.1f}{r['Actual']:<12.2f}{r['Error']:<12+.3f}{r['Error_Pct']:<12+.2f}")
    print("==================================================")

def generate_plot(results):
    try:
        import matplotlib.pyplot as plt
        
        targets = [r['Target'] for r in results]
        errors = [r['Error'] for r in results]
        pct_errors = [r['Error_Pct'] for r in results]
        
        # Create a figure with two subplots: absolute error and percent error
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
        
        # Plot 1: Absolute Error
        ax1.plot(targets, errors, marker='o', color='#1a73e8', linewidth=2, label='Measured Error')
        ax1.axhline(0, color='red', linestyle='--', alpha=0.6)
        ax1.fill_between(targets, -0.05, 0.05, color='#e8f0fe', alpha=0.4, label='Common Settle Tolerance (±0.05g)')
        ax1.set_ylabel('Absolute Error (g)', fontsize=11)
        ax1.set_title('Dispensing Accuracy Test Results (Pump 1)', fontsize=14, fontweight='bold', pad=10)
        ax1.grid(True, linestyle=':', alpha=0.6)
        ax1.legend(loc='upper right')
        
        # Plot 2: Percentage Error
        ax2.plot(targets, pct_errors, marker='s', color='#34a853', linewidth=2, label='Measured Error %')
        ax2.axhline(0, color='red', linestyle='--', alpha=0.6)
        ax2.set_xlabel('Target Weight (g)', fontsize=12)
        ax2.set_ylabel('Percentage Error (%)', fontsize=11)
        ax2.grid(True, linestyle=':', alpha=0.6)
        ax2.legend(loc='upper right')
        
        plt.tight_layout()
        plot_file = "accuracy_plot.png"
        plt.savefig(plot_file, dpi=300, bbox_inches='tight')
        print(f"Plot successfully saved to: {plot_file}")
    except ImportError:
        print("\nNote: matplotlib is not installed. To automatically generate plots from this data, run:")
        print("      C:\\Users\\littl\\AppData\\Roaming\\uv\\python\cpython-3.14.3-windows-x86_64-none\\python.exe -m pip install matplotlib")

if __name__ == "__main__":
    run_test()
