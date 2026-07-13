#!/usr/bin/env python3
"""
Multi-pump gravimetric accuracy tests.

Examples:
  python accuracy_test.py --mode all-pumps --target 10 --tol 0.01
  python accuracy_test.py --mode sweep --pump 1
  python accuracy_test.py --mode multi-sweep --pumps 1,2,4,6 --targets 10,20,50 --tol 0.01

Acceptance (plan Phase 6): |error| <= 0.01g at 10g for pumps 1-7.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from typing import Iterable, List, Optional, Sequence

PUMP_COUNT = 7
DEFAULT_TOL_G = 0.01
DEFAULT_SWEEP_TARGETS = [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0]


def find_serial_ports() -> List[str]:
    import serial.tools.list_ports
    return [p.device for p in serial.tools.list_ports.comports()]


def select_port(preferred: Optional[str] = None) -> str:
    ports = find_serial_ports()
    if not ports:
        print("No active COM ports found. Please connect your Arduino Board.")
        sys.exit(1)
    if preferred:
        if preferred in ports:
            return preferred
        print(f"Preferred port {preferred} not found; available: {ports}")
        sys.exit(1)

    print("Available COM Ports:")
    for idx, p in enumerate(ports):
        print(f"[{idx}] {p}")

    while True:
        port_idx = input("Select Port index (default 0): ").strip()
        if not port_idx:
            return ports[0]
        try:
            idx = int(port_idx)
            if 0 <= idx < len(ports):
                return ports[idx]
            print(f"Index out of range. Must be between 0 and {len(ports) - 1}.")
        except ValueError:
            print("Invalid input. Please enter a valid number.")


def open_serial(port: str):
    import serial
    print(f"\nConnecting to {port} at 9600 Baud...")
    try:
        ser = serial.Serial(port, 9600, timeout=1.0)
        time.sleep(3)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)
    return ser


def handshake(ser) -> None:
    ser.reset_input_buffer()
    print("Waiting for serial telemetry handshake...")
    start_time = time.time()
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if "TELEMETRY:" in line:
            print(f"Handshake OK: {line}")
            return
        if time.time() - start_time > 5.0:
            print("Warning: Telemetry handshake timed out, trying to proceed anyway...")
            return


def readline_text(ser) -> str:
    return ser.readline().decode("utf-8", errors="ignore").strip()


def wait_for(ser, predicates: Sequence[str], timeout_s: float = 600.0) -> str:
    """Read until any predicate substring appears. Returns matching line."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = readline_text(ser)
        if not line:
            continue
        print(f"[Serial] {line}")
        if "!!! ERROR:" in line or "FAULT" in line:
            raise RuntimeError(f"Controller fault while waiting: {line}")
        for pred in predicates:
            if pred in line:
                return line
    raise TimeoutError(f"Timed out waiting for any of: {predicates}")


def send_line(ser, text: str) -> None:
    ser.write(f"{text}\n".encode())


def tare_scale(ser, settle_s: float = 1.0) -> None:
    """
    Software-tare the controller scale (USB command 'T').
    Waits briefly so a fresh cup on the scale is stable, then tares and
    confirms telemetry reports near 0 g.
    """
    print(f"Waiting {settle_s:.1f}s for scale settle before tare...")
    time.sleep(settle_s)
    # Drain stale lines so wait_for sees the tare ack.
    ser.reset_input_buffer()
    print("Sending scale tare (T)...")
    send_line(ser, "T")
    wait_for(ser, ["Scale Software Tared."], timeout_s=10.0)

    # Confirm post-tare weight is near zero (read a few telemetry samples).
    deadline = time.time() + 3.0
    last = None
    while time.time() < deadline:
        line = readline_text(ser)
        if not line:
            continue
        if line.startswith("TELEMETRY:"):
            try:
                last = float(line.split(":", 1)[1].split(",")[0])
            except (IndexError, ValueError):
                continue
            if abs(last) <= 0.05:
                print(f"Tare OK (telemetry {last:.2f}g).")
                return
    if last is None:
        print("Warning: No telemetry after tare; proceeding anyway.")
    else:
        print(
            f"Warning: Post-tare weight is {last:.2f}g (expected ~0). "
            "Empty/replace the cup and consider re-running."
        )


def parse_pump_list(text: str) -> List[int]:
    pumps: List[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        n = int(part)
        if n < 1 or n > PUMP_COUNT:
            raise ValueError(f"Pump {n} out of range 1-{PUMP_COUNT}")
        pumps.append(n)
    if not pumps:
        raise ValueError("No pumps specified")
    return pumps


def parse_targets(text: Optional[str]) -> List[float]:
    if not text:
        return list(DEFAULT_SWEEP_TARGETS)
    return [float(x.strip()) for x in text.split(",") if x.strip()]


def send_recipe_targets(ser, targets_by_pump: Sequence[float]) -> None:
    """targets_by_pump is length PUMP_COUNT, grams (0 skips)."""
    assert len(targets_by_pump) == PUMP_COUNT
    for pump_id, target in enumerate(targets_by_pump, start=1):
        time.sleep(0.35)
        print(f"Sending Pump {pump_id} target: {target:.2f}g")
        send_line(ser, f"{target:.2f}")
        wait_for(ser, [f"Pump {pump_id} Target set to:"], timeout_s=30.0)


def collect_dispense_results(ser, expected_pumps: Iterable[int]) -> dict:
    """
    Monitor until sequence completes. Return {pump_id: dispensed_g}
    for pumps that logged 'Pump N done. Dispensed:'.
    """
    expected = set(expected_pumps)
    results: dict = {}
    print("Dispensing... monitoring output...")
    while True:
        line = readline_text(ser)
        if not line:
            continue
        print(f"[Serial] {line}")
        if "!!! ERROR:" in line:
            raise RuntimeError(line)
        m = re.search(r"Pump\s+(\d+)\s+done\.\s+Dispensed:\s*([-\d\.]+)", line)
        if m:
            pump_id = int(m.group(1))
            results[pump_id] = float(m.group(2))
        if "Entire multi-pump dispensing sequence completed successfully!" in line:
            break
    missing = expected - set(results.keys())
    if missing:
        raise RuntimeError(f"Missing dispense results for pumps: {sorted(missing)}")
    return results


def evaluate(results: List[dict], tol_g: float) -> bool:
    ok = True
    print("\n==================================================")
    print(f"{'Pump':<6}{'Target':<10}{'Actual':<10}{'Error':<12}{'Pass':<6}")
    for r in results:
        err = r["Error"]
        passed = abs(err) <= tol_g
        ok = ok and passed
        print(
            f"{r['Pump']:<6}"
            f"{r['Target']:<10.2f}"
            f"{r['Actual']:<10.3f}"
            f"{err:<+12.3f}"
            f"{'YES' if passed else 'NO':<6}"
        )
    print(f"Tolerance: ±{tol_g:.3f}g  Overall: {'PASS' if ok else 'FAIL'}")
    print("==================================================")
    return ok


def save_csv(path: str, results: List[dict]) -> None:
    with open(path, mode="w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Pump", "Target (g)", "Actual (g)", "Error (g)", "Pass"])
        for r in results:
            writer.writerow(
                [
                    r["Pump"],
                    r["Target"],
                    f"{r['Actual']:.3f}",
                    f"{r['Error']:.3f}",
                    "YES" if abs(r["Error"]) <= r["Tol"] else "NO",
                ]
            )
    print(f"Results saved to CSV: {path}")


def generate_plot(results: List[dict], title: str) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nNote: matplotlib is not installed; skipping plot.")
        return

    targets = [r["Target"] for r in results]
    errors = [r["Error"] for r in results]
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(targets, errors, marker="o", linewidth=2, label="Measured Error")
    ax.axhline(0, color="red", linestyle="--", alpha=0.6)
    tol = results[0]["Tol"] if results else DEFAULT_TOL_G
    ax.fill_between(targets, -tol, tol, color="#e8f0fe", alpha=0.5, label=f"±{tol:.3f}g")
    ax.set_xlabel("Target Weight (g)")
    ax.set_ylabel("Absolute Error (g)")
    ax.set_title(title)
    ax.grid(True, linestyle=":", alpha=0.6)
    ax.legend(loc="best")
    plt.tight_layout()
    plot_file = "accuracy_plot.png"
    plt.savefig(plot_file, dpi=300, bbox_inches="tight")
    print(f"Plot saved to: {plot_file}")


def run_all_pumps(ser, target_g: float, tol_g: float, do_tare: bool = True) -> List[dict]:
    print(f"\n=== All-pumps acceptance: {target_g}g ±{tol_g}g on pumps 1-{PUMP_COUNT} ===")
    # Skip between/final mix so pump-to-pump timing stays in the accuracy path.
    send_line(ser, "MIX DISABLE")
    time.sleep(0.3)
    send_line(ser, "MIX STOP")
    time.sleep(0.3)
    if do_tare:
        tare_scale(ser)
    targets = [target_g] * PUMP_COUNT
    send_recipe_targets(ser, targets)
    dispensed = collect_dispense_results(ser, range(1, PUMP_COUNT + 1))
    rows = []
    for pump_id in range(1, PUMP_COUNT + 1):
        actual = dispensed[pump_id]
        rows.append(
            {
                "Pump": pump_id,
                "Target": target_g,
                "Actual": actual,
                "Error": actual - target_g,
                "Tol": tol_g,
            }
        )
    return rows


def run_sweep(
    ser, pump_id: int, targets: Sequence[float], tol_g: float, do_tare: bool = True
) -> List[dict]:
    print(f"\n=== Sweep Pump {pump_id}: targets {list(targets)} ===")
    rows = []
    for target in targets:
        print(f"\n--- Pump {pump_id} target {target}g ---")
        if do_tare:
            tare_scale(ser)
        recipe = [0.0] * PUMP_COUNT
        recipe[pump_id - 1] = float(target)
        send_recipe_targets(ser, recipe)
        dispensed = collect_dispense_results(ser, [pump_id])
        actual = dispensed[pump_id]
        rows.append(
            {
                "Pump": pump_id,
                "Target": float(target),
                "Actual": actual,
                "Error": actual - float(target),
                "Tol": tol_g,
            }
        )
        time.sleep(2.0)
    return rows


def run_multi_sweep(
    ser,
    pumps: Sequence[int],
    targets: Sequence[float],
    tol_g: float,
    do_tare: bool = True,
) -> List[dict]:
    rows: List[dict] = []
    for pump_id in pumps:
        rows.extend(run_sweep(ser, pump_id, targets, tol_g, do_tare=do_tare))
    return rows


def sample_stdev(values: Sequence[float]) -> float:
    n = len(values)
    if n < 2:
        return 0.0
    mean = sum(values) / n
    return (sum((v - mean) ** 2 for v in values) / (n - 1)) ** 0.5


def summarize_characterize(
    samples: List[dict], target_g: float, pct_spec: float = 0.1
) -> List[dict]:
    """
    Per-pump stats and min mass for relative accuracy (default 0.1% by weight).

    Expanded single-shot uncertainty U = |bias| + 2*s (approx. k=2 coverage of bias+repeatability).
    Min mass for pct_spec: M_min = U / (pct_spec/100)  so that U/M <= pct_spec/100.
    """
    summaries = []
    pump_ids = sorted({int(r["Pump"]) for r in samples})
    rel = pct_spec / 100.0
    for pump_id in pump_ids:
        rows = [r for r in samples if int(r["Pump"]) == pump_id]
        errors = [float(r["Error"]) for r in rows]
        abs_errs = [abs(e) for e in errors]
        n = len(errors)
        mean_err = sum(errors) / n
        s = sample_stdev(errors)
        u_mean = s / (n**0.5) if n > 0 else 0.0
        # Single-dispense expanded uncertainty (conservative)
        u_expanded = abs(mean_err) + 2.0 * s
        max_abs = max(abs_errs) if abs_errs else 0.0
        # Capability uses the larger of expanded U and observed worst-case |error|
        limit_of_error = max(u_expanded, max_abs)
        m_min = (limit_of_error / rel) if rel > 0 else float("inf")
        summaries.append(
            {
                "Pump": pump_id,
                "N": n,
                "Target_g": target_g,
                "Mean_g": sum(float(r["Actual"]) for r in rows) / n,
                "MeanError_g": mean_err,
                "StdDev_g": s,
                "StdErrMean_g": u_mean,
                "MaxAbsError_g": max_abs,
                "ExpandedU_g": u_expanded,
                "LimitOfError_g": limit_of_error,
                "MinMass_0p1pct_g": m_min,
                "RelAccuracy_at_target_pct": (limit_of_error / target_g) * 100.0
                if target_g
                else 0.0,
            }
        )
    return summaries


def run_characterize(
    ser,
    pumps: Sequence[int],
    target_g: float,
    reps: int,
    do_tare: bool = True,
) -> List[dict]:
    print(
        f"\n=== Characterize pumps {list(pumps)}: {reps} reps @ {target_g}g each ==="
    )
    # Keep mixing off so replicates stay practical.
    send_line(ser, "MIX DISABLE")
    time.sleep(0.3)
    send_line(ser, "MIX TIME BETWEEN 0")
    time.sleep(0.3)
    send_line(ser, "MIX TIME FINAL 0")
    time.sleep(0.3)

    samples: List[dict] = []
    for pump_id in pumps:
        for rep in range(1, reps + 1):
            print(f"\n--- Pump {pump_id}  rep {rep}/{reps}  target {target_g}g ---")
            # Ensure prompt is at pump 1 before sending a single-pump recipe.
            send_line(ser, "S")
            try:
                wait_for(ser, ["Pump 1 Enter weight", "Enter weight (g):"], timeout_s=15.0)
            except TimeoutError:
                print("Warning: did not see pump-1 prompt after reset; continuing.")

            if do_tare:
                tare_scale(ser)

            recipe = [0.0] * PUMP_COUNT
            recipe[pump_id - 1] = float(target_g)
            send_recipe_targets(ser, recipe)
            dispensed = collect_dispense_results(ser, [pump_id])
            actual = dispensed[pump_id]
            err = actual - float(target_g)
            samples.append(
                {
                    "Pump": pump_id,
                    "Rep": rep,
                    "Target": float(target_g),
                    "Actual": actual,
                    "Error": err,
                    "Tol": float(target_g) * 0.001,  # 0.1% of target
                }
            )
            print(
                f"Result P{pump_id} R{rep}: {actual:.3f}g  error {err:+.3f}g"
            )
            time.sleep(1.0)
    return samples


def save_characterize_csv(samples_path: str, summary_path: str, samples: List[dict], summaries: List[dict]) -> None:
    with open(samples_path, mode="w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Pump", "Rep", "Target (g)", "Actual (g)", "Error (g)"])
        for r in samples:
            writer.writerow(
                [
                    r["Pump"],
                    r["Rep"],
                    r["Target"],
                    f"{r['Actual']:.3f}",
                    f"{r['Error']:.3f}",
                ]
            )
    print(f"Sample results saved to: {samples_path}")

    with open(summary_path, mode="w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "Pump",
                "N",
                "Target (g)",
                "Mean Actual (g)",
                "Mean Error (g)",
                "Std Dev (g)",
                "Std Err of Mean (g)",
                "Max |Error| (g)",
                "Expanded U |bias|+2s (g)",
                "Limit of Error (g)",
                "Rel accuracy at target (%)",
                "Min mass for 0.1% (g)",
            ]
        )
        for s in summaries:
            writer.writerow(
                [
                    s["Pump"],
                    s["N"],
                    f"{s['Target_g']:.2f}",
                    f"{s['Mean_g']:.3f}",
                    f"{s['MeanError_g']:.3f}",
                    f"{s['StdDev_g']:.3f}",
                    f"{s['StdErrMean_g']:.3f}",
                    f"{s['MaxAbsError_g']:.3f}",
                    f"{s['ExpandedU_g']:.3f}",
                    f"{s['LimitOfError_g']:.3f}",
                    f"{s['RelAccuracy_at_target_pct']:.3f}",
                    f"{s['MinMass_0p1pct_g']:.1f}",
                ]
            )
    print(f"Summary saved to: {summary_path}")


def print_characterize_summary(summaries: List[dict], pct_spec: float = 0.1) -> None:
    print("\n==================================================")
    print(f"Characterization summary ({pct_spec}% by-weight capability)")
    print(
        f"{'Pump':<6}{'MeanErr':<10}{'s':<8}{'Max|e|':<9}{'U_exp':<9}{'M_min@0.1%':<12}"
    )
    for s in summaries:
        print(
            f"{s['Pump']:<6}"
            f"{s['MeanError_g']:<+10.3f}"
            f"{s['StdDev_g']:<8.3f}"
            f"{s['MaxAbsError_g']:<9.3f}"
            f"{s['ExpandedU_g']:<9.3f}"
            f"{s['MinMass_0p1pct_g']:<12.1f}"
        )
    print("U_exp = |mean error| + 2*s (single-shot expanded)")
    print("M_min = LimitOfError / 0.001  (mass where LOE is 0.1% of dose)")
    print("==================================================")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Multi-pump dispensing accuracy test")
    p.add_argument(
        "--mode",
        choices=("all-pumps", "sweep", "multi-sweep", "characterize"),
        default="all-pumps",
        help="all-pumps / sweep / multi-sweep / characterize (N reps per pump for uncertainty).",
    )
    p.add_argument("--port", default=None, help="COM port (e.g. COM5). Interactive if omitted.")
    p.add_argument("--pump", type=int, default=1, help="Pump id for --mode sweep (1-7).")
    p.add_argument("--pumps", default="1,2,3,4,5,6,7", help="Comma list for multi-sweep/characterize.")
    p.add_argument("--target", type=float, default=10.0, help="Mass for all-pumps/characterize.")
    p.add_argument("--targets", default=None, help="Comma list for sweep modes.")
    p.add_argument("--reps", type=int, default=5, help="Replicates per pump for characterize mode.")
    p.add_argument("--tol", type=float, default=DEFAULT_TOL_G, help="Pass/fail absolute error (g).")
    p.add_argument("--csv", default="accuracy_results.csv", help="CSV output path.")
    p.add_argument(
        "--no-tare",
        action="store_true",
        help="Skip automatic scale tare before each recipe.",
    )
    return p


def main() -> int:
    args = build_parser().parse_args()
    do_tare = not args.no_tare
    print("==================================================")
    print("Multi-Pump Accuracy Test")
    print(f"Mode: {args.mode}  Tolerance: ±{args.tol}g  Auto-tare: {do_tare}")
    print("==================================================")

    port = select_port(args.port)
    ser = open_serial(port)
    try:
        handshake(ser)
        if args.mode == "all-pumps":
            results = run_all_pumps(ser, args.target, args.tol, do_tare=do_tare)
            title = f"All-pumps accuracy @ {args.target}g"
            save_csv(args.csv, results)
            generate_plot(results, title)
            ok = evaluate(results, args.tol)
            return 0 if ok else 1
        if args.mode == "sweep":
            if args.pump < 1 or args.pump > PUMP_COUNT:
                print(f"Pump must be 1-{PUMP_COUNT}")
                return 2
            targets = parse_targets(args.targets)
            results = run_sweep(ser, args.pump, targets, args.tol, do_tare=do_tare)
            title = f"Dispensing Accuracy Sweep (Pump {args.pump})"
            save_csv(args.csv, results)
            generate_plot(results, title)
            ok = evaluate(results, args.tol)
            return 0 if ok else 1
        if args.mode == "multi-sweep":
            pumps = parse_pump_list(args.pumps)
            targets = parse_targets(args.targets) if args.targets else [10.0, 20.0, 50.0]
            results = run_multi_sweep(ser, pumps, targets, args.tol, do_tare=do_tare)
            title = f"Multi-pump sweep ({','.join(map(str, pumps))})"
            save_csv(args.csv, results)
            generate_plot(results, title)
            ok = evaluate(results, args.tol)
            return 0 if ok else 1

        # characterize
        if args.reps < 2:
            print("--reps must be >= 2 for uncertainty")
            return 2
        pumps = parse_pump_list(args.pumps)
        samples = run_characterize(
            ser, pumps, args.target, args.reps, do_tare=do_tare
        )
        summaries = summarize_characterize(samples, args.target, pct_spec=0.1)
        samples_csv = args.csv if args.csv != "accuracy_results.csv" else "accuracy_characterize_samples.csv"
        summary_csv = "accuracy_characterize_summary.csv"
        save_characterize_csv(samples_csv, summary_csv, samples, summaries)
        print_characterize_summary(summaries, pct_spec=0.1)
        # Also write sample rows to plot path helper using flat results shape
        plot_rows = [
            {
                "Pump": r["Pump"],
                "Target": r["Target"],
                "Actual": r["Actual"],
                "Error": r["Error"],
                "Tol": r["Tol"],
            }
            for r in samples
        ]
        generate_plot(plot_rows, f"Characterize {args.reps}x @ {args.target}g")
        # Pass if every pump's limit of error at this target is within 0.1% of target
        limit = args.target * 0.001
        ok = all(s["LimitOfError_g"] <= limit for s in summaries)
        print(
            f"\n0.1% band at {args.target}g is ±{limit:.3f}g. "
            f"All pumps within band: {'YES' if ok else 'NO'}"
        )
        return 0 if ok else 1
    except (RuntimeError, TimeoutError, ValueError) as exc:
        print(f"\nTest aborted: {exc}")
        return 2
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
