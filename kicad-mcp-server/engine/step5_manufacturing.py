"""
Step 5: Manufacturing Output.

Generates gerbers, STEP 3D model, BOM CSV, and pick-and-place CSV.
"""
import csv
import os
from typing import Dict, Any

from models import KiCadProjectSpec
from kicad.cli_runner import run_kicad_cli, KiCADNotFoundError


def run(spec: KiCadProjectSpec, project_dir: str, **kwargs) -> Dict[str, str]:
    """
    Export manufacturing files.

    Args:
        spec: KiCadProjectSpec
        project_dir: Project directory
        **kwargs: Optional cli_path for kicad-cli

    Returns:
        Dict of artifact name -> file path
    """
    project_name = spec.project_name
    pcb_file = f"{project_dir}/{project_name}.kicad_pcb"
    results = {}
    cli_path = kwargs.get("cli_path")

    # Export gerbers
    gerbers_dir = os.path.join(project_dir, "gerbers")
    os.makedirs(gerbers_dir, exist_ok=True)
    try:
        run_kicad_cli(
            "pcb", "export", "gerbers",
            extra_args=["--output", gerbers_dir, pcb_file],
            timeout=120,
            cli_path=cli_path,
        )
        results["gerbers"] = gerbers_dir
    except KiCADNotFoundError:
        results["gerbers"] = ""
        results["gerber_error"] = "kicad-cli not found. Install KiCAD 8.0+."
    except Exception as e:
        results["gerbers"] = ""
        results["gerber_error"] = f"Gerber export error: {str(e)}"

    # Export STEP
    step_path = os.path.join(project_dir, f"{project_name}.step")
    try:
        run_kicad_cli(
            "pcb", "export", "step",
            extra_args=["--output", step_path, pcb_file],
            timeout=120,
            cli_path=cli_path,
        )
        results["step_3d"] = step_path
    except KiCADNotFoundError:
        results["step_3d"] = ""
    except Exception as e:
        results["step_3d"] = ""
        results["step_error"] = f"STEP export error: {str(e)}"

    # Generate BOM CSV
    bom_path = os.path.join(project_dir, "bom.csv")
    try:
        _generate_bom_csv(spec, bom_path)
        results["bom"] = bom_path
    except Exception as e:
        results["bom"] = ""
        results["bom_error"] = f"BOM generation error: {str(e)}"

    # Generate pick-and-place CSV
    placement_path = os.path.join(project_dir, "placement.csv")
    try:
        _generate_placement_csv(spec, placement_path)
        results["placement"] = placement_path
    except Exception as e:
        results["placement"] = ""
        results["placement_error"] = f"Placement generation error: {str(e)}"

    return results


def _generate_bom_csv(spec: KiCadProjectSpec, filepath: str):
    """Generate a BOM CSV grouped by value + footprint."""
    groups: Dict[str, dict] = {}
    for comp in spec.components:
        key = f"{comp.value}|{comp.footprint_name}"
        if key not in groups:
            groups[key] = {"references": [], "value": comp.value, "footprint": comp.footprint_name, "mpn": comp.mpn or ""}
        groups[key]["references"].append(comp.reference)

    with open(filepath, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["Ref(s)", "Value", "Footprint", "MPN", "Qty"])
        for key, group in sorted(groups.items()):
            writer.writerow([
                ", ".join(group["references"]),
                group["value"],
                group["footprint"],
                group["mpn"],
                len(group["references"]),
            ])


def _generate_placement_csv(spec: KiCadProjectSpec, filepath: str):
    """Generate a pick-and-place CSV from component positions."""
    with open(filepath, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["Ref", "Mid-X", "Mid-Y", "Layer", "Rotation"])
        for comp in spec.components:
            pos = comp.position or {"x": 0, "y": 0}
            # All components are on Front (F) layer in this simple model
            layer = "F"
            rotation = 0
            writer.writerow([
                comp.reference,
                pos.get("x", 0),
                pos.get("y", 0),
                layer,
                rotation,
            ])
