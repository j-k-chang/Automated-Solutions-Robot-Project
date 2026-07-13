#!/usr/bin/env python3
"""
Automate DROP CAL on the robot and report g/drop and steps/drop per pump.

Requires firmware that supports: DROP CAL <pump> [count]

Examples:
  python drop_characterize.py --port COM6 --drops 15
  python drop_characterize.py --port COM6 --pumps 1,2,7 --drops 20
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from typing import Dict, List, Optional


PUMP_COUNT = 7
EVENT_RE = re.compile(
    r"DROP_EVENT:pump=(\d+),idx=(\d+),steps=(\d+),mass_g=([-\d\.]+)"
)
SUMMARY_RE = re.compile(
    r"DROP_SUMMARY:pump=(\d+),n=(\d+),mean_mass_g=([-\d\.]+),std_mass_g=([-\d\.]+),"
    r"mean_steps=([-\d\.]+),std_steps=([-\d\.]+),total_steps=(\d+),reason=(\S+)"
)


def find_ports() -> List[str]:
    import serial.tools.list_ports
    return [p.device for p in serial.tools.list_ports.comports()]


def open_serial(port: str):
    import serial
    print(f"Connecting to {port} at 9600 baud...")
    ser = serial.Serial(port, 9600, timeout=1.0)
    time.sleep(3.0)
    return ser


def send(ser, text: str) -> None:
    ser.write(f"{text}\n".encode())


def readline(ser) -> str:
    return ser.readline().decode("utf-8", errors="ignore").strip()


def wait_for(ser, needle: str, timeout_s: float = 30.0) -> str:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = readline(ser)
        if not line:
            continue
        print(f"[Serial] {line}")
        if needle in line:
            return line
        if "!!! ERROR:" in line:
            raise RuntimeError(line)
    raise TimeoutError(f"Timed out waiting for: {needle}")


def tare(ser) -> None:
    time.sleep(0.8)
    ser.reset_input_buffer()
    send(ser, "T")
    wait_for(ser, "Scale Software Tared.", timeout_s=10.0)
    print("Tare OK.")


def parse_pumps(text: str) -> List[int]:
    pumps = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        n = int(part)
        if n < 1 or n > PUMP_COUNT:
            raise ValueError(f"Pump {n} out of range")
        pumps.append(n)
    return pumps


def run_drop_cal(ser, pump: int, drops: int) -> Dict:
    events = []
    summary = None

    send(ser, "S")
    time.sleep(0.4)
    ser.reset_input_buffer()
    tare(ser)

    print(f"\n=== DROP CAL pump {pump}, target {drops} drops ===")
    send(ser, f"DROP CAL {pump} {drops}")

    deadline = time.time() + 900.0  # 15 min safety per pump
    while time.time() < deadline:
        line = readline(ser)
        if not line:
            continue
        print(f"[Serial] {line}")

        if "ERROR: DROP CAL" in line:
            raise RuntimeError(line)
        if "DROP CAL" in line and "not" in line.lower():
            raise RuntimeError(
                "Controller rejected DROP CAL. Flash firmware that includes DROP CAL support."
            )

        if "DROP_REJECT" in line:
            continue

        m = EVENT_RE.search(line)
        if m:
            mass = float(m.group(4))
            # Firmware should already reject oversize/stream, but filter defensively.
            if mass < 0.018 or mass > 0.100:
                print(f"  (ignored out-of-range event {mass:.4f}g)")
                continue
            events.append(
                {
                    "pump": int(m.group(1)),
                    "idx": int(m.group(2)),
                    "steps": int(m.group(3)),
                    "mass_g": mass,
                }
            )
            continue

        m = SUMMARY_RE.search(line)
        if m:
            summary = {
                "pump": int(m.group(1)),
                "n": int(m.group(2)),
                "mean_mass_g": float(m.group(3)),
                "std_mass_g": float(m.group(4)),
                "mean_steps": float(m.group(5)),
                "std_steps": float(m.group(6)),
                "total_steps": int(m.group(7)),
                "reason": m.group(8),
            }
            break

        if "!!! EMERGENCY" in line:
            raise RuntimeError("Emergency stop during DROP CAL")

    if summary is None:
        raise TimeoutError(f"No DROP_SUMMARY for pump {pump}")

    # Discard first event as tip priming / meniscus warm-up when we have enough.
    stable = events[1:] if len(events) >= 4 else events
    if stable:
        masses = [e["mass_g"] for e in stable]
        steps = [e["steps"] for e in stable]
        n = len(stable)
        mean_m = sum(masses) / n
        mean_s = sum(steps) / n
        std_m = (
            (sum((x - mean_m) ** 2 for x in masses) / (n - 1)) ** 0.5 if n > 1 else 0.0
        )
        std_s = (
            (sum((x - mean_s) ** 2 for x in steps) / (n - 1)) ** 0.5 if n > 1 else 0.0
        )
        summary["stable_n"] = n
        summary["stable_mean_mass_g"] = mean_m
        summary["stable_std_mass_g"] = std_m
        summary["stable_mean_steps"] = mean_s
        summary["stable_std_steps"] = std_s
        # Conservative half-drop for final pulse sizing
        summary["half_drop_g"] = mean_m * 0.5
        summary["suggested_final_pulse_g"] = max(0.005, mean_m * 0.45)
    else:
        summary["stable_n"] = 0
        summary["stable_mean_mass_g"] = summary["mean_mass_g"]
        summary["stable_std_mass_g"] = summary["std_mass_g"]
        summary["stable_mean_steps"] = summary["mean_steps"]
        summary["stable_std_steps"] = summary["std_steps"]
        summary["half_drop_g"] = summary["mean_mass_g"] * 0.5
        summary["suggested_final_pulse_g"] = max(0.005, summary["mean_mass_g"] * 0.45)

    summary["events"] = events
    return summary


def write_csvs(events_path: str, summary_path: str, results: List[Dict]) -> None:
    with open(events_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Pump", "DropIdx", "Steps", "Mass_g"])
        for r in results:
            for e in r["events"]:
                w.writerow([e["pump"], e["idx"], e["steps"], f"{e['mass_g']:.4f}"])

    with open(summary_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "Pump",
                "N_raw",
                "N_stable",
                "MeanMass_g",
                "StdMass_g",
                "MeanSteps",
                "StdSteps",
                "HalfDrop_g",
                "SuggestedFinalPulse_g",
                "Reason",
            ]
        )
        for r in results:
            w.writerow(
                [
                    r["pump"],
                    r["n"],
                    r.get("stable_n", r["n"]),
                    f"{r.get('stable_mean_mass_g', r['mean_mass_g']):.4f}",
                    f"{r.get('stable_std_mass_g', r['std_mass_g']):.4f}",
                    f"{r.get('stable_mean_steps', r['mean_steps']):.1f}",
                    f"{r.get('stable_std_steps', r['std_steps']):.1f}",
                    f"{r['half_drop_g']:.4f}",
                    f"{r['suggested_final_pulse_g']:.4f}",
                    r["reason"],
                ]
            )


def print_summary(results: List[Dict]) -> None:
    print("\n==================================================")
    print("Drop characterization (stable = drop 2..N)")
    print(
        f"{'Pump':<6}{'MeanMass':<11}{'s_mass':<9}{'MeanSteps':<11}{'HalfDrop':<10}{'FinalPulse':<11}"
    )
    for r in results:
        print(
            f"{r['pump']:<6}"
            f"{r.get('stable_mean_mass_g', 0):<11.4f}"
            f"{r.get('stable_std_mass_g', 0):<9.4f}"
            f"{r.get('stable_mean_steps', 0):<11.1f}"
            f"{r['half_drop_g']:<10.4f}"
            f"{r['suggested_final_pulse_g']:<11.4f}"
        )
    print("==================================================")
    print("If 2*std_mass is near or above 0.01g, ±0.01g single-shot is unlikely.")
    print("Use HalfDrop / SuggestedFinalPulse as trim pulse caps near target.")


def main() -> int:
    p = argparse.ArgumentParser(description="Characterize drop mass and steps/drop")
    p.add_argument("--port", default=None)
    p.add_argument("--pumps", default="1,2,3,4,5,6,7")
    p.add_argument("--drops", type=int, default=15, help="Drops to capture per pump")
    p.add_argument("--events-csv", default="drop_events.csv")
    p.add_argument("--summary-csv", default="drop_summary.csv")
    args = p.parse_args()

    ports = find_ports()
    if not ports:
        print("No COM ports found.")
        return 1
    port = args.port or ports[0]
    if args.port and args.port not in ports:
        print(f"Port {args.port} not found. Available: {ports}")
        return 1

    pumps = parse_pumps(args.pumps)
    ser = open_serial(port)
    results: List[Dict] = []
    try:
        # Probe for DROP CAL support with a friendly error.
        send(ser, "S")
        time.sleep(0.5)
        ser.reset_input_buffer()
        for pump in pumps:
            try:
                results.append(run_drop_cal(ser, pump, args.drops))
            except Exception as exc:
                print(f"Pump {pump} failed: {exc}")
                send(ser, "S")
                time.sleep(0.5)
        if not results:
            return 2
        write_csvs(args.events_csv, args.summary_csv, results)
        print(f"Wrote {args.events_csv} and {args.summary_csv}")
        print_summary(results)
        return 0
    finally:
        send(ser, "S")
        time.sleep(0.2)
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
