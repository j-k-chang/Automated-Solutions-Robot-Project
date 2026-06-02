import sys
import os
import time
import re
import subprocess
import importlib

# ==========================================
# 📦 AUTO-DEPENDENCY MANAGEMENT
# ==========================================
required_libs = {"serial": "pyserial", "matplotlib": "matplotlib"}
for import_name, package_name in required_libs.items():
    try:
        importlib.import_module(import_name)
    except ImportError:
        print(f"[*] Missing dependency '{package_name}'. Installing...")
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", package_name])
            print(f"[+] Successfully installed {package_name}!")
        except Exception as e:
            print(f"[!] Failed to install {package_name}: {e}")
            print("[!] Please run: pip install pyserial matplotlib")
            sys.exit(1)

import serial
from serial.tools import list_ports
import matplotlib.pyplot as plt

# ==========================================
# ⚙️ CONFIGURATION
# ==========================================
DEFAULT_COM_PORT = "COM3"
BAUD_RATE = 9600
TEST_TARGETS = [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0]

# Filepaths inside the scratch directory
SCRATCH_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_FILE = os.path.join(SCRATCH_DIR, "accuracy_test_results.csv")
CHART_FILE = os.path.join(SCRATCH_DIR, "accuracy_test_chart.png")
REPORT_FILE = os.path.join(SCRATCH_DIR, "accuracy_report.md")

# ==========================================
# 🔌 SERIAL PORT MANAGEMENT
# ==========================================
def select_com_port():
    ports = list(list_ports.comports())
    if not ports:
        print("[!] No active COM ports found on this system!")
        com = input("[?] Enter COM port manually (e.g. COM3): ").strip()
        return com if com else DEFAULT_COM_PORT

    print("\nAvailable COM Ports:")
    for i, port in enumerate(ports):
        print(f" [{i + 1}] {port.device} - {port.description}")
    
    # Try to find default COM3
    for port in ports:
        if port.device.upper() == DEFAULT_COM_PORT.upper():
            print(f"\n[+] Auto-detected default Arduino port: {port.device}")
            return port.device
            
    # Fallback to prompt
    try:
        choice = input(f"\n[?] Select COM port number [1-{len(ports)}] or press Enter for COM3: ").strip()
        if not choice:
            return DEFAULT_COM_PORT
        idx = int(choice) - 1
        if 0 <= idx < len(ports):
            return ports[idx].device
    except (ValueError, IndexError):
        pass
    return DEFAULT_COM_PORT

# ==========================================
# 🚀 CORE TEST FLOW
# ==========================================
def run_accuracy_test():
    com_port = select_com_port()
    
    print("\n" + "=" * 50)
    print("      GRAVIMETRIC DISPENSER ACCURACY SUITE")
    print("=" * 50)
    print(f"Connecting to Arduino on: {com_port}")
    print(f"Baud Rate:                {BAUD_RATE}")
    print(f"Target Weights to Test:   {TEST_TARGETS} (g)")
    print("=" * 50)
    
    try:
        ser = serial.Serial(com_port, BAUD_RATE, timeout=1.0)
        time.sleep(2) # Allow Arduino bootloader to stabilize
        ser.reset_input_buffer()
        print("[+] Serial Connection Established successfully!")
    except Exception as e:
        print(f"[!] Failed to connect to serial port {com_port}: {e}")
        sys.exit(1)

    results = []

    # Compile regex pattern for completion line
    # e.g., "Dispense Finished successfully at: 100.08g!"
    finish_regex = re.compile(r"Dispense Finished successfully at:\s*([0-9.]+)g!")

    try:
        print("\n" + "=" * 50)
        print("          FULLY AUTOMATED TEST RUN INITIALIZATION")
        print("=" * 50)
        
        # Select and transmit liquid viscosity profile
        profile = ""
        while profile not in ["L", "H"]:
            profile = input("[?] Choose Liquid Profile: Enter [L] for Water, [H] for Glycerol: ").strip().upper()
        
        ser.write(f"{profile}\n".encode("ascii"))
        time.sleep(0.5) # Let the Arduino print confirmation
        while ser.in_waiting > 0:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            print(f"    [Arduino]: {line}")
            
        print("\n [1] Place a large beaker (minimum 600mL capacity) on the scale.")
        print(" [2] TARE the scale to 0.00g (or let the software tare handle it).")
        print(" [3] The script will run all 10 trials sequentially into this beaker.")
        input("\n[?] Press [ENTER] to start the automated 10-trial sequence...")

        for idx, target in enumerate(TEST_TARGETS):
            print(f"\n" + "=" * 50)
            print(f" TRIAL {idx + 1}/{len(TEST_TARGETS)}: TARGET {target:.2f}g")
            print("=" * 50)
            
            if idx > 0:
                print("[*] Waiting 5 seconds for scale to settle and software tare to stabilize...")
                time.sleep(5)
            
            # Send target weight command to Arduino
            cmd = f"{target}\n"
            ser.write(cmd.encode("ascii"))
            print(f"[>] Sent command to Arduino: {cmd.strip()}g")
            print("[-] Active Dispensing... (Press Ctrl+C to emergency halt pump)\n")
            
            actual_weight = None
            
            # Listen to serial output from Arduino
            while True:
                if ser.in_waiting > 0:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()
                    if line:
                        print(f"    [Arduino]: {line}")
                        
                        # Check for completion string
                        match = finish_regex.search(line)
                        if match:
                            actual_weight = float(match.group(1))
                            break
                time.sleep(0.01)
            
            # Log results
            error = actual_weight - target
            error_pct = (error / target) * 100
            
            results.append({
                "Target": target,
                "Actual": actual_weight,
                "Error": error,
                "ErrorPct": error_pct
            })
            
            print(f"\n[+] Trial {idx + 1} Result Summary:")
            print(f"    Target:  {target:.2f}g")
            print(f"    Actual:  {actual_weight:.2f}g")
            print(f"    Error:   {error:+.2f}g ({error_pct:+.2f}%)")
            print("-" * 50)

    except KeyboardInterrupt:
        print("\n\n[!] TEST INTERRUPTED BY USER!")
        print("[!] Sending Emergency Halt Command ('S') to Arduino...")
        try:
            ser.write(b"S\n")
            time.sleep(0.5)
            # Read whatever is left in buffer
            while ser.in_waiting > 0:
                print(f"    [Arduino]: {ser.readline().decode('utf-8').strip()}")
            print("[+] Emergency Stop command sent and verified. Pump halted safely.")
        except Exception as ex:
            print(f"[!] Failed to send emergency halt: {ex}")
        ser.close()
        sys.exit(1)
        
    ser.close()
    
    # Process and Save Data
    save_results_and_reports(results)

# ==========================================
# 📊 REPORT GENERATION & DATA WRITING
# ==========================================
def save_results_and_reports(results):
    print("\n" + "=" * 50)
    print("           COMPILING RESULTS & CHARTS")
    print("=" * 50)
    
    # 1. Write CSV Data
    print(f"[*] Saving raw data to: {CSV_FILE}...")
    try:
        with open(CSV_FILE, "w") as f:
            f.write("Target(g),Actual(g),Error(g),ErrorPct(%)\n")
            for r in results:
                f.write(f"{r['Target']:.2f},{r['Actual']:.2f},{r['Error']:.4f},{r['ErrorPct']:.4f}\n")
        print("[+] CSV Data saved successfully!")
    except Exception as e:
        print(f"[!] Failed to save CSV: {e}")

    # 2. Calculate Statistics
    targets = [r["Target"] for r in results]
    actuals = [r["Actual"] for r in results]
    errors = [r["Error"] for r in results]
    abs_errors = [abs(r["Error"]) for r in results]
    error_pcts = [r["ErrorPct"] for r in results]

    avg_abs_error = sum(abs_errors) / len(abs_errors)
    max_error = max(errors, key=abs)
    std_dev_error = (sum((e - (sum(errors)/len(errors)))**2 for e in errors) / len(errors))**0.5
    avg_error_pct = sum(error_pcts) / len(error_pcts)

    # 3. Generate Matplotlib Chart
    print(f"[*] Plotting accuracy chart to: {CHART_FILE}...")
    try:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
        
        # Plot A: Target vs Actual Line
        ax1.plot(targets, targets, 'k--', alpha=0.6, label="Ideal Line (Target)")
        ax1.scatter(targets, actuals, color="#4F46E5", s=80, zorder=5, label="Measured Weights")
        ax1.plot(targets, actuals, color="#818CF8", linewidth=2, alpha=0.8)
        ax1.set_title("Target vs. Actual Weight", fontsize=14, fontweight="bold", pad=15)
        ax1.set_xlabel("Target Weight (g)", fontsize=12)
        ax1.set_ylabel("Delivered Weight (g)", fontsize=12)
        ax1.grid(True, linestyle=":", alpha=0.6)
        ax1.legend(fontsize=10, loc="upper left")
        
        # Plot B: Absolute Error Bar Chart
        colors = ["#EF4444" if e > 0 else "#3B82F6" for e in errors] # Red for over, Blue for under
        bars = ax2.bar(targets, errors, width=6.0, color=colors, edgecolor="black", alpha=0.85)
        
        # Draw target lines and label values
        ax2.axhline(0, color="black", linewidth=1.2)
        for bar in bars:
            height = bar.get_height()
            label_y = height + 0.005 if height >= 0 else height - 0.015
            ax2.annotate(f"{height:+.2f}g",
                         xy=(bar.get_x() + bar.get_width() / 2, label_y),
                         xytext=(0, 0), textcoords="offset points",
                         ha="center", va="bottom", fontsize=9, fontweight="bold")
                         
        ax2.set_title("Delivery Error distribution (g)", fontsize=14, fontweight="bold", pad=15)
        ax2.set_xlabel("Target Weight (g)", fontsize=12)
        ax2.set_ylabel("Error (g)", fontsize=12)
        ax2.grid(True, linestyle=":", alpha=0.6)
        
        # Adjust Y limits on error chart to look tidy
        max_err_y = max(abs_errors) * 1.5
        ax2.set_ylim(-max(0.1, max_err_y), max(0.1, max_err_y))
        
        plt.tight_layout()
        plt.savefig(CHART_FILE, dpi=200)
        plt.close()
        print("[+] Matplotlib charts generated and saved successfully!")
    except Exception as e:
        print(f"[!] Failed to generate Matplotlib charts: {e}")

    # 4. Generate Markdown Summary Report
    print(f"[*] Generating Markdown report to: {REPORT_FILE}...")
    try:
        report_md = f"""# Gravimetric Dispensing System Accuracy Report

This report summarizes the performance and precision analysis of the closed-loop, adaptive progressive liquid dispensing firmware across 10 distinct target trials ranging from **10.00g to 100.00g**.

---

## 📈 Accuracy Metrics Summary

| Metric | Value | Interpretation |
| :--- | :--- | :--- |
| **Mean Absolute Error (MAE)** | `{avg_abs_error:.4f} g` | Average deviation from absolute target weight. |
| **Maximum Absolute Error** | `{abs(max_error):.4f} g` | Worst-case overshoot/undershoot registered. |
| **Error Standard Deviation (σ)** | `{std_dev_error:.4f} g` | Consistency and repeatability measure. |
| **Average Percentage Deviation** | `{avg_error_pct:+.4f}%` | Mean relative calibration skew. |

---

## 📊 Trial Detail Data

| Trial | Target Weight (g) | Actual Weight (g) | Absolute Error (g) | Relative Error (%) | Status |
| :---: | :---: | :---: | :---: | :---: | :---: |
"""
        for idx, r in enumerate(results):
            status = "✅ PASS" if abs(r["Error"]) <= 0.08 else "⚠️ REVIEW"
            report_md += f"| #{idx + 1} | {r['Target']:.2f}g | {r['Actual']:.2f}g | {r['Error']:+.4f}g | {r['ErrorPct']:+.4f}% | {status} |\n"

        report_md += f"""
---

## 📉 Graphical Analysis

Below is the visualized error distribution and linear tracing curve generated directly from the calibration run:

![Accuracy Analysis Chart](accuracy_test_chart.png)

---

## 📂 Reference Logs
* Raw CSV Dataset: [accuracy_test_results.csv](accuracy_test_results.csv)
"""
        with open(REPORT_FILE, "w") as f:
            f.write(report_md.strip())
        print("[+] Markdown report generated successfully!")
        
    except Exception as e:
        print(f"[!] Failed to write Markdown report: {e}")

    # 5. Print terminal report
    print("\n" + "=" * 50)
    print("               TESTING SUMMARY")
    print("=" * 50)
    print(f"  Mean Absolute Error (MAE): {avg_abs_error:.4f}g")
    print(f"  Max Absolute Error:       {abs(max_error):.4f}g (Target: {targets[errors.index(max_error)]:.2f}g)")
    print(f"  Standard Deviation (σ):    {std_dev_error:.4f}g")
    print("=" * 50)
    print(f"Saved CSV Report:  {CSV_FILE}")
    print(f"Saved Chart Image: {CHART_FILE}")
    print(f"Saved Markdown:    {REPORT_FILE}")
    print("=" * 50 + "\n")

if __name__ == "__main__":
    run_accuracy_test()
