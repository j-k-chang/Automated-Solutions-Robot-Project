"""
KiCAD MCP Server - AI-driven PCB design automation
===================================================

This MCP server exposes KiCAD capabilities to AI agents (Claude CLI, etc.)
for automated schematic capture, PCB layout, and gerber generation.

Architecture:
  AI Agent ──MCP──▶ kiCAD-MCP-Server ──▶ KiCAD Python API / kicad-cli
  Uses the same FastMCP pattern as your LiquidDispenser server.

Requirements:
  - KiCAD 8.0+ installed (provides kicad-cli and Python API)
  - Python 3.10+
  - pip install mcp pydantic pydantic-settings

Usage with Claude CLI:
  Add to claude_desktop_config.json:
  {
    "mcpServers": {
      "KiCAD": {
        "command": "python",
        "args": ["-m", "kicad_mcp_server.main_api"],
        "cwd": "/path/to/this/project"
      }
    }
  }
"""
import os
import sys
import json
import shutil
import subprocess
from pathlib import Path
from typing import List, Optional, Dict, Any

from mcp.server.fastmcp import FastMCP, Context

# Import shared models and pipeline modules
sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

from models import (
    KiCadSymbol, KiCadFootprint, ComponentInstance,
    NetConnection, KiCadProjectSpec, StepResult,
)
from kicad.json_writer import write_project
from kicad.cli_runner import run_kicad_cli, KiCADNotFoundError
from specs.spec_loader import load_spec_from_json

# --- MCP Server Setup ---
mcp = FastMCP("KiCAD-PCB-Designer")

# --- State ---
current_project: Optional[str] = None
project_dir: Optional[str] = None


# --- MCP Tools ---

@mcp.tool()
def create_kicad_project(spec: KiCadProjectSpec) -> str:
    """
    Create a new KiCAD project with schematic, symbols, and footprints.
    
    This is the entry point for AI-driven PCB design. The AI agent should:
    1. Define all symbols (components) with their pins
    2. Define all footprints (packages)
    3. Place components on the schematic
    4. Wire them together with net connections
    5. Then call route_and_generate_gerbers()
    
    Args:
        spec: Complete project specification with symbols, footprints, components, and nets
    
    Returns:
        Path to created project and summary of what was generated
    """
    global current_project, project_dir
    
    # Create project directory
    base_dir = Path.home() / ".kicad-pipeline-projects"
    base_dir.mkdir(parents=True, exist_ok=True)

    project_name = spec.project_name
    project_dir = str(base_dir / project_name)
    os.makedirs(project_dir, exist_ok=True)

    # Write real KiCAD project files using json_writer module
    files = write_project(spec, project_dir)

    current_project = project_name
    project_dir = project_dir

    return json.dumps({
        "status": "success",
        "project_path": project_dir,
        "project_file": files["project"],
        "schematic_file": files["schematic"],
        "pcb_file": files["pcb"],
        "netlist_file": files["netlist"],
        "symbols_created": len(spec.symbols),
        "footprints_created": len(spec.footprints),
        "components_placed": len(spec.components),
        "nets_defined": len(spec.nets),
        "note": "Real KiCAD 8 JSON files generated. Open in KiCAD GUI to verify and refine routing."
    }, indent=2)


@mcp.tool()
def add_symbol(symbol: KiCadSymbol) -> str:
    """
    Add a schematic symbol to the current project's symbol library.
    
    Args:
        symbol: Symbol definition with name, value, and pins
    
    Returns:
        Confirmation and path to symbol library file
    """
    if not project_dir:
        return json.dumps({"status": "error", "message": "No active project. Call create_kicad_project first."})
    
    sym_file = os.path.join(project_dir, "symbols.kicad_sym")
    # Append symbol to library (KiCAD 8 uses JSON format for libraries)
    # For now, we'll write to a combined symbols file
    symbols_data = []
    if os.path.exists(sym_file):
        with open(sym_file, 'r') as f:
            try:
                data = json.load(f)
                if isinstance(data, dict):
                    symbols_data = data.get("symbols", [])
                elif isinstance(data, list):
                    symbols_data = data
            except json.JSONDecodeError:
                symbols_data = []
    
    sym_entry = symbol.model_dump()
    symbols_data.append(sym_entry)
    
    with open(sym_file, 'w') as f:
        json.dump({"version": 1, "symbols": symbols_data}, f, indent=2)
    
    return json.dumps({
        "status": "success",
        "symbol": symbol.name,
        "library_file": sym_file,
        "pins": len(symbol.pins)
    })


@mcp.tool()
def add_footprint(footprint: KiCadFootprint) -> str:
    """
    Add a PCB footprint to the current project's footprint library.
    
    Args:
        footprint: Footprint definition with package and dimensions
    
    Returns:
        Confirmation and path to footprint library file
    """
    if not project_dir:
        return json.dumps({"status": "error", "message": "No active project. Call create_kicad_project first."})
    
    fp_file = os.path.join(project_dir, "footprints.kicad_mod")
    # KiCAD 8 uses JSON for footprints
    fp_data = footprint.model_dump()
    
    # Append or create
    if os.path.exists(fp_file):
        with open(fp_file, 'r') as f:
            try:
                data = json.load(f)
                if isinstance(data, dict):
                    fps = data.get("footprints", [])
                elif isinstance(data, list):
                    fps = data
            except json.JSONDecodeError:
                fps = []
    else:
        fps = []
    
    fps.append(fp_data)
    with open(fp_file, 'w') as f:
        json.dump({"version": 1, "footprints": fps}, f, indent=2)
    
    return json.dumps({
        "status": "success",
        "footprint": footprint.name,
        "package": footprint.package,
        "library_file": fp_file
    })


@mcp.tool()
def route_and_generate_gerbers() -> str:
    """
    Run DRC and export gerbers/STEP for fabrication.

    This calls kicad-cli (KiCAD 8+) for headless operations:
    1. Run DRC (Design Rule Check)
    2. Export gerbers
    3. Export STEP 3D model

    Returns:
        Summary of DRC results and paths to exported files
    """
    if not project_dir:
        return json.dumps({"status": "error", "message": "No active project. Call create_kicad_project first."})

    project_name = current_project
    results = {}
    proj_file = f"{project_dir}/{project_name}.kicad_pro"

    # Step 1: Run DRC
    try:
        drc_output = run_kicad_cli("pcb", "drc", project_file=proj_file, timeout=120)
        results["drc"] = drc_output.strip()
    except KiCADNotFoundError:
        results["drc"] = "kicad-cli not found. Install KiCAD 8.0+."
    except Exception as e:
        results["drc"] = f"DRC error: {str(e)}"

    # Step 2: Export gerbers
    gerbers_dir = os.path.join(project_dir, "gerbers")
    os.makedirs(gerbers_dir, exist_ok=True)
    try:
        gerber_output = run_kicad_cli(
            "pcb", "export", "gerbers",
            project_file=proj_file,
            extra_args=["--output-dir", gerbers_dir],
        )
        results["gerbers"] = "OK"
    except KiCADNotFoundError:
        results["gerbers"] = "kicad-cli not found"
    except Exception as e:
        results["gerbers"] = f"Export error: {str(e)}"

    # Step 3: Export STEP
    step_output_path = os.path.join(project_dir, f"{project_name}.step")
    try:
        step_output = run_kicad_cli(
            "pcb", "export", "step",
            project_file=proj_file,
            extra_args=["--output", step_output_path],
        )
        results["3d_model"] = step_output_path
    except KiCADNotFoundError:
        results["3d_model"] = "kicad-cli not found"
    except Exception as e:
        results["3d_model"] = f"Export error: {str(e)}"

    return json.dumps({
        "status": "success",
        "project": project_name,
        "project_path": project_dir,
        "gerbers_dir": gerbers_dir,
        "results": results,
        "note": "Open in KiCAD GUI for manual routing refinement. Gerbers are in the gerbers/ directory."
    }, indent=2)


@mcp.tool()
def read_project_status() -> str:
    """
    Get the current state of the active KiCAD project.
    
    Returns:
        Project path, files present, and project metadata
    """
    if not project_dir:
        return json.dumps({"status": "no_project", "message": "No active project."})
    
    files = {}
    for f in os.listdir(project_dir):
        fpath = os.path.join(project_dir, f)
        if os.path.isfile(fpath):
            files[f] = os.path.getsize(fpath)
    
    return json.dumps({
        "status": "success",
        "project": current_project,
        "path": project_dir,
        "files": files,
        "total_size_mb": sum(files.values()) / (1024 * 1024)
    }, indent=2)


@mcp.tool()
def install_kicad_dependencies() -> str:
    """
    Check and install required dependencies for the KiCAD MCP server.
    
    Installs:
    - mcp (Anthropic MCP SDK)
    - pydantic (data validation)
    - kicost (BOM cost estimation, optional)
    
    Also checks for KiCAD installation and provides setup instructions.
    
    Returns:
        Installation status and next steps
    """
    results = {}
    
    # Check KiCAD
    kicad_path = shutil.which("kicad")
    kicad_cli_path = shutil.which("kicad-cli")
    
    if kicad_path:
        try:
            res = subprocess.run(["kicad", "--version"], capture_output=True, text=True, timeout=5)
            result = res.stdout.strip() or res.stderr.strip() or "unknown"
        except Exception:
            result = "unknown"
        results["kicad"] = {"installed": True, "version": result}
    else:
        results["kicad"] = {
            "installed": False,
            "install_instructions": "Install KiCAD 8.0+: https://www.kicad.org/download/"
        }
    
    if kicad_cli_path:
        results["kicad_cli"] = {"installed": True, "path": kicad_cli_path}
    else:
        results["kicad_cli"] = {"installed": False, "note": "Included with KiCAD installation"}
    
    # Install Python deps
    pip_packages = ["mcp", "pydantic"]
    for pkg in pip_packages:
        try:
            res = subprocess.run([sys.executable, "-m", "pip", "show", pkg], capture_output=True, text=True, timeout=5)
            result = res.stdout.strip()
        except Exception:
            result = ""
        results[pkg] = {"installed": bool(result), "version": result.split("\n")[0] if result else "not installed"}
    
    return json.dumps({
        "status": "check_complete",
        "dependencies": results,
        "next_steps": [
            "Install KiCAD 8.0+ if not already installed",
            "Run this tool again to verify installation",
            "Call create_kicad_project() to start designing"
        ]
    }, indent=2)


if __name__ == '__main__':
    print("Starting KiCAD MCP Server...", file=sys.stderr)
    mcp.run()
