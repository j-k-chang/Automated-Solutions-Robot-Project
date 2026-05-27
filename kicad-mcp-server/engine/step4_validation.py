"""
Step 4: Design Validation.

Runs DRC, ERC, and netlist verification.
"""
import json
import os
from typing import Dict, Any

from models import KiCadProjectSpec
from kicad.cli_runner import run_kicad_cli, run_kicad_cli_json, KiCADNotFoundError


def run(spec: KiCadProjectSpec, project_dir: str, **kwargs) -> Dict[str, str]:
    """
    Run DRC, ERC, and netlist verification.

    Args:
        spec: KiCadProjectSpec
        project_dir: Project directory
        **kwargs: Optional cli_path for kicad-cli

    Returns:
        Dict of artifact name -> file path (DRC report, netlist)
    """
    project_name = spec.project_name
    pcb_file = f"{project_dir}/{project_name}.kicad_pcb"
    sch_file = f"{project_dir}/{project_name}.kicad_sch"
    results = {}

    # Run DRC
    drc_path = f"{project_dir}/drc_report.json"
    try:
        drc_output = run_kicad_cli(
            "pcb", "drc",
            extra_args=["--output", drc_path, "--format", "json", pcb_file],
            timeout=120,
            **{k: v for k, v in kwargs.items() if k == "cli_path"},
        )
        results["drc_report"] = drc_path
        results["drc_output"] = drc_output.strip()
    except KiCADNotFoundError:
        results["drc_report"] = ""
        results["drc_output"] = "kicad-cli not found. Install KiCAD 8.0+ for DRC."
    except Exception as e:
        results["drc_report"] = ""
        results["drc_output"] = f"DRC error: {str(e)}"

    # Run ERC on schematic
    try:
        erc_output = run_kicad_cli(
            "sch", "erc",
            extra_args=[sch_file],
            timeout=60,
            **{k: v for k, v in kwargs.items() if k == "cli_path"},
        )
        results["erc_output"] = erc_output.strip()
    except KiCADNotFoundError:
        results["erc_output"] = "kicad-cli not found"
    except Exception as e:
        results["erc_output"] = f"ERC error: {str(e)}"

    # Generate netlist
    netlist_path = f"{project_dir}/{project_name}_validated.kicad_netlist.xml"
    try:
        netlist_output = run_kicad_cli(
            "sch", "export", "netlist",
            extra_args=["--output", netlist_path, "--format", "kicadxml", sch_file],
            timeout=60,
            **{k: v for k, v in kwargs.items() if k == "cli_path"},
        )
        results["netlist"] = netlist_path
        results["netlist_output"] = netlist_output.strip()
    except KiCADNotFoundError:
        results["netlist"] = ""
        results["netlist_output"] = "kicad-cli not found"
    except Exception as e:
        results["netlist"] = ""
        results["netlist_output"] = f"Netlist error: {str(e)}"

    return results
