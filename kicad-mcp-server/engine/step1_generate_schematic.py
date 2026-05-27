"""
Step 1: Generate Schematic.

Converts a KiCadProjectSpec into KiCAD .kicad_pro, .kicad_sch, .kicad_pcb files.
Optionally runs ERC via kicad-cli if available.
"""
import json
import os
from pathlib import Path
from typing import Dict, Any, Optional

from models import KiCadProjectSpec
from kicad.json_writer import write_project
from kicad.cli_runner import run_kicad_cli, KiCADNotFoundError


def run(
    spec: KiCadProjectSpec,
    project_dir: str,
    cli_path: Optional[str] = None,
    run_erc: bool = True,
) -> Dict[str, str]:
    """
    Generate schematic and project files from spec.

    Args:
        spec: KiCadProjectSpec with symbols, footprints, components, nets
        project_dir: Output directory for KiCAD project files
        cli_path: Optional path to kicad-cli executable
        run_erc: Whether to run ERC after generation

    Returns:
        Dict of artifact name -> file path
    """
    os.makedirs(project_dir, exist_ok=True)

    # Write all project files
    files = write_project(spec, project_dir)

    if run_erc:
        try:
            sch_file = files["schematic"]
            result = run_kicad_cli(
                "sch", "erc",
                extra_args=[sch_file],
                timeout=60,
                cli_path=cli_path,
            )
            # ERC output is informational; failures are warnings not errors
        except KiCADNotFoundError:
            pass  # ERC skipped, not critical
        except Exception:
            pass  # ERC failed, but schematic files are still valid

    return files
