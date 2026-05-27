"""
CLI entry point for the KiCAD pipeline.

Usage:
    python -m kicad_mcp_server.cli init <project_name> <spec.json>
    python -m kicad_mcp_server.cli run <spec.json> [--steps 1,2,3,4,5] [--project-dir DIR]
"""
import argparse
import json
import sys
from pathlib import Path

# Allow importing from the same directory
sys.path.insert(0, str(Path(__file__).parent))

from models import KiCadProjectSpec
from specs.spec_loader import load_spec_from_json, load_spec_from_generator
from engine.orchestrator import PipelineOrchestrator


def cmd_init(args):
    """Initialize a new project from a spec file."""
    spec = load_spec_from_json(args.spec)
    project_dir = args.project_dir or str(Path.home() / ".kicad-pipeline-projects" / spec.project_name)
    orchestrator = PipelineOrchestrator(spec, project_dir)
    print(f"Project initialized: {project_dir}")
    print(f"  Components: {len(spec.components)}")
    print(f"  Nets: {len(spec.nets)}")
    print(f"  Symbols: {len(spec.symbols)}")


def cmd_run(args):
    """Run the pipeline."""
    spec = load_spec_from_json(args.spec)

    project_dir = args.project_dir
    if not project_dir:
        project_dir = str(Path.home() / ".kicad-pipeline-projects" / spec.project_name)

    # Parse step selection
    steps_map = {
        "1": "generate_schematic",
        "2": "component_selection",
        "3": "pcb_layout",
        "4": "validation",
        "5": "manufacturing",
    }
    if args.steps:
        steps = [steps_map[s.strip()] for s in args.steps.split(",") if s.strip() in steps_map]
    else:
        steps = None  # Run all

    orchestrator = PipelineOrchestrator(spec, project_dir)
    results = orchestrator.run_pipeline(steps)

    # Print summary
    summary = orchestrator.get_summary()
    print(f"\nPipeline complete for: {summary['project']}")
    print(f"  Project dir: {summary['project_dir']}")
    print(f"  Steps completed: {summary['steps_completed']}")
    print(f"  Steps failed: {summary['steps_failed']}")

    for step, detail in summary["step_details"].items():
        status = "OK" if detail["success"] else "FAIL"
        if detail["error"]:
            print(f"  [{status}] {step}: {detail['error']}")
        else:
            print(f"  [{status}] {step}: {list(detail['artifacts'].keys())}")

    # Exit with error if any step failed
    if summary["steps_failed"] > 0:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="KiCAD Pipeline CLI")
    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # init subcommand
    init_parser = subparsers.add_parser("init", help="Initialize a new project")
    init_parser.add_argument("spec", help="Path to spec JSON file")
    init_parser.add_argument("--project-dir", help="Output directory")
    init_parser.set_defaults(func=cmd_init)

    # run subcommand
    run_parser = subparsers.add_parser("run", help="Run the pipeline")
    run_parser.add_argument("spec", help="Path to spec JSON file")
    run_parser.add_argument("--steps", help="Comma-separated step numbers (1-5)")
    run_parser.add_argument("--project-dir", help="Output directory")
    run_parser.set_defaults(func=cmd_run)

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        sys.exit(1)

    args.func(args)


if __name__ == "__main__":
    main()
