# KiCAD MCP Server - AI-Driven PCB Design Pipeline

An MCP (Model Context Protocol) server + CLI pipeline for automated KiCAD PCB design.

## Architecture

```
AI Agent (Claude CLI) → MCP Protocol → KiCAD MCP Server → KiCAD 8.0+ (kicad-cli)
                              ↓
                    Python Pipeline Orchestrator
                              ↓
         kicad/   sourcing/   engine/   specs/
```

## Quick Start

```bash
pip install -r requirements.txt

# Run pipeline from CLI
python -m cli run <spec.json> [--steps 1,2,3,4,5] [--project-dir DIR]

# Initialize a project
python -m cli init <spec.json> [--project-dir DIR]

# Start MCP server
python -m main_api
```

## Pipeline Steps

| Step | Description | Status |
|------|-------------|--------|
| 1 | Generate Schematic — produces `.kicad_pro`, `.kicad_sch`, `.kicad_pcb` | Implemented |
| 2 | Component Selection — DigiKey/Mouser lookup | Stub (Phase 4) |
| 3 | PCB Layout — footprint placement, routing | Stub (Phase 2) |
| 4 | Design Validation — DRC, ERC, netlist verification | Implemented (requires kicad-cli) |
| 5 | Manufacturing Output — gerbers, STEP, BOM, placement | Implemented (requires kicad-cli) |

## CLI Usage

```bash
# Run all steps
python -m cli run kicad_14ch_dispensing.json

# Run specific steps
python -m cli run kicad_14ch_dispensing.json --steps 1,4,5

# Custom output directory
python -m cli run kicad_14ch_dispensing.json --project-dir ./my-project
```

## MCP Tools

| Tool | Description |
|------|-------------|
| `create_kicad_project(spec)` | Create KiCAD project with schematic, symbols, footprints, nets |
| `add_symbol(symbol)` | Add schematic symbol to library |
| `add_footprint(footprint)` | Add PCB footprint to library |
| `route_and_generate_gerbers()` | Run DRC, export gerbers, STEP model |
| `read_project_status()` | Check current project state |
| `install_kicad_dependencies()` | Check/install KiCAD and Python deps |

## Project Structure

```
kicad-mcp-server/
├── main_api.py              # MCP server (FastMCP)
├── cli.py                   # CLI entry point
├── models.py                # Pydantic data models
├── config.py                # pydantic-settings configuration
├── requirements.txt
├── README.md
├── kicad/
│   ├── cli_runner.py        # kicad-cli subprocess wrapper
│   └── json_writer.py       # KiCAD 8 JSON file generators
├── engine/
│   ├── orchestrator.py      # Pipeline orchestration
│   ├── step1_generate_schematic.py
│   ├── step2_component_selection.py
│   ├── step3_pcb_layout.py
│   ├── step4_validation.py
│   └── step5_manufacturing.py
├── sourcing/
│   ├── digikey_client.py
│   ├── mouser_client.py
│   └── part_matcher.py
└── specs/
    └── spec_loader.py       # Spec file loader
```

## Spec Input

The pipeline accepts JSON spec files in two formats:

1. **Generator format** (from `generate_*.py` scripts): top-level `project_name`, `symbols`, `footprints`, `components`, `nets`
2. **KiCAD JSON format** (from `kicad_14ch_dispensing.json`): nested under `schematic.components` and `schematic.connections`

## Output Files

After a full pipeline run:

```
~/.kicad-pipeline-projects/<project>/
├── <project>.kicad_pro      # KiCAD project file
├── <project>.kicad_sch      # Schematic (KiCAD 8 JSON)
├── <project>.kicad_pcb      # PCB (KiCAD 8 JSON)
├── <project>.kicad_netlist.xml
├── gerbers/                 # Gerber files (requires kicad-cli)
├── <project>.step           # 3D model (requires kicad-cli)
├── bom.csv                  # Bill of materials
├── placement.csv            # Pick-and-place data
└── pipeline_state.json      # Pipeline execution state
```

## Requirements

- KiCAD 8.0+ (for DRC, gerber export, STEP export)
- Python 3.10+
- `pip install -r requirements.txt`

## Claude CLI Integration

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "KiCAD": {
      "command": "python",
      "args": ["-m", "main_api"],
      "cwd": "/path/to/kicad-mcp-server"
    }
  }
}
```

## Troubleshooting

- **"kicad-cli not found"** — Install KiCAD 8.0+: https://www.kicad.org/download/
- **Schematic opens empty** — The schematic is a valid KiCAD 8 JSON file. Open in KiCAD GUI to verify symbols and connections.
- **DRC fails** — The PCB has no routing (step 3 is a stub). Add routing in KiCAD GUI before running DRC.
- **Permission denied** — Ensure output directory exists: `mkdir -p ~/.kicad-pipeline-projects`
