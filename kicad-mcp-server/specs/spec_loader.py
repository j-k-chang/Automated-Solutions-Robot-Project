"""
Spec loader for the KiCAD pipeline.

Loads KiCadProjectSpec from JSON files or Python generator outputs.
"""
import json
import importlib.util
from pathlib import Path
from typing import Optional

from models import KiCadProjectSpec


def load_spec_from_json(filepath: str) -> KiCadProjectSpec:
    """
    Load a KiCadProjectSpec from a JSON file.

    Supports both the generate_*.py output format and the kicad_14ch_dispensing.json format.

    Args:
        filepath: Path to JSON file

    Returns:
        KiCadProjectSpec

    Raises:
        ValueError: If the JSON structure doesn't match expected format
    """
    path = Path(filepath)
    if not path.exists():
        raise FileNotFoundError(f"Spec file not found: {filepath}")

    data = json.loads(path.read_text(encoding="utf-8"))

    # Handle the generate_*.py format (top-level: project_name, title, symbols, footprints, components, nets)
    if "project_name" in data and "symbols" in data:
        return _parse_generator_format(data)

    # Handle the kicad_14ch_dispensing.json format (nested under schematic)
    if "schematic" in data:
        return _parse_kicad_json_format(data)

    raise ValueError(f"Unknown spec format in {filepath}. Expected 'project_name' + 'symbols' or 'schematic' keys.")


def _parse_generator_format(data: dict) -> KiCadProjectSpec:
    """Parse the generate_*.py output format."""
    from models import KiCadSymbol, KiCadFootprint, ComponentInstance, NetConnection

    return KiCadProjectSpec(
        project_name=data["project_name"],
        title=data.get("title", data["project_name"]),
        author=data.get("author", "AI Agent"),
        date=data.get("date", ""),
        board_dimensions=data.get("board_dimensions", {}),
        symbols=[KiCadSymbol(**s) for s in data.get("symbols", [])],
        footprints=[KiCadFootprint(**f) for f in data.get("footprints", [])],
        components=[ComponentInstance(**c) for c in data.get("components", [])],
        nets=[NetConnection(**n) for n in data.get("nets", [])],
    )


def map_pin(ref: str, pin_name: str) -> str:
    ref_upper = ref.upper()
    pin_upper = pin_name.upper()
    
    # TMC2209 sockets (J5 - J18)
    if ref_upper.startswith("J") and ref_upper[1:].isdigit():
        val = int(ref_upper[1:])
        if 5 <= val <= 18:
            mapping = {
                "EN": "1",
                "MS1": "3",
                "MS2": "5",
                "RX": "7",
                "TX": "9",
                "CLK": "11",
                "STEP": "13",
                "DIR": "15",
                "VM": "2",
                "GND": "16",
                "A2": "6",
                "2A": "6",
                "A1": "8",
                "2B": "8",
                "B1": "10",
                "1B": "10",
                "B2": "12",
                "1A": "12",
                "VDD": "14",
                "VIO": "14",
                "SW": "1",
            }
            if pin_upper == "GND":
                return "16"
            return mapping.get(pin_upper, pin_name)
            
    # MAX3232 module J2
    if ref_upper == "J2":
        mapping = {
            "VCC": "1",
            "GND": "2",
            "TX": "3",
            "RX": "4"
        }
        return mapping.get(pin_upper, pin_name)
        
    return pin_name


def _parse_kicad_json_format(data: dict) -> KiCadProjectSpec:
    """Parse the kicad_14ch_dispensing.json format (nested under 'schematic')."""
    from models import KiCadSymbol, KiCadFootprint, ComponentInstance, NetConnection

    schematic = data.get("schematic", {})
    components_data = schematic.get("components", [])
    connections_data = schematic.get("connections", [])

    # Build symbols from components (each unique libsource+value combo)
    symbols = []
    seen_symbols = set()
    for comp in components_data:
        libsource = comp.get("libsource", {})
        key = (libsource.get("lib", ""), libsource.get("part", ""), comp.get("value", ""))
        if key not in seen_symbols:
            seen_symbols.add(key)
            pins = []  # kicad_14ch_dispensing.json doesn't have pin definitions
            symbols.append(KiCadSymbol(
                name=libsource.get("part", comp.get("value", "")),
                value=comp.get("value", ""),
                reference=comp.get("ref", "U"),
                pins=pins,
            ))

    # Build footprints from components
    footprints = []
    seen_fps = set()
    for comp in components_data:
        fp = comp.get("footprint", "")
        if fp not in seen_fps:
            seen_fps.add(fp)
            footprints.append(KiCadFootprint(
                name=comp.get("value", ""),
                package=fp,
                dimensions={},
            ))

    # Build components
    components = []
    for comp in components_data:
        pos = None
        pcb_placement = data.get("pcb", {}).get("placement", {})
        ref = comp.get("ref", "")
        # Try to find placement from PCB data
        for key in ["arduino", "max3232"]:
            if key in pcb_placement:
                place = pcb_placement[key]
                if ref == "U1" or ref == "J2":
                    pos = {"x": place.get("x", 0), "y": place.get("y", 0)}

        components.append(ComponentInstance(
            reference=comp.get("ref", ""),
            symbol_name=comp.get("value", ""),
            footprint_name=comp.get("footprint", ""),
            value=comp.get("value", ""),
            position=pos,
        ))

    # Build nets from connections
    nets = []
    for conn in connections_data:
        net_name = conn.get("net", "")
        nodes = conn.get("nodes", [])
        if len(nodes) >= 2:
            ref0 = nodes[0][0]
            pin0 = nodes[0][1] if len(nodes[0]) > 1 else ""
            mapped_pin0 = map_pin(ref0, pin0) if pin0 else ""
            from_ref = f"{ref0}:{mapped_pin0}" if mapped_pin0 else ref0
            
            for node in nodes[1:]:
                refN = node[0]
                pinN = node[1] if len(node) > 1 else ""
                mapped_pinN = map_pin(refN, pinN) if pinN else ""
                to_ref = f"{refN}:{mapped_pinN}" if mapped_pinN else refN
                
                nets.append(NetConnection(
                    net_name=net_name,
                    from_ref=from_ref,
                    to_ref=to_ref,
                ))

    pcb = data.get("pcb", {})

    return KiCadProjectSpec(
        project_name=data.get("project", {}).get("name", "unknown"),
        title=data.get("project", {}).get("description", ""),
        author=data.get("project", {}).get("author", "Unknown"),
        date=data.get("project", {}).get("date", ""),
        board_dimensions={
            "width_mm": pcb.get("board_size", {}).get("width", 100),
            "height_mm": pcb.get("board_size", {}).get("height", 70),
        },
        symbols=symbols,
        footprints=footprints,
        components=components,
        nets=nets,
    )


def load_spec_from_generator(generator_path: str) -> KiCadProjectSpec:
    """
    Load a spec by running a Python generator script.

    Args:
        generator_path: Path to .py file that prints JSON spec

    Returns:
        KiCadProjectSpec
    """
    import subprocess
    import sys

    result = subprocess.run(
        [sys.executable, generator_path],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Generator failed: {result.stderr[:200]}")

    data = json.loads(result.stdout.strip())
    return _parse_generator_format(data)
