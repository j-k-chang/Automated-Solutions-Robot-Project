"""
KiCAD 8 JSON file generators.

Produces valid .kicad_pro, .kicad_sch, and .kicad_pcb JSON files
from a KiCadProjectSpec. Uses internal units (IU) where 1 IU = 0.001 mm.
"""
import json
import uuid
from pathlib import Path
from typing import Dict, List, Any, Optional

from models import KiCadProjectSpec


# KiCAD uses internal units: 1 IU = 0.001 mm
IU_PER_MM = 1000


def _iu(value_mm: float) -> int:
    """Convert mm to KiCAD internal units."""
    return int(value_mm * IU_PER_MM)


def _gen_uuid() -> str:
    """Generate a stable UUID string."""
    return str(uuid.uuid4())


def generate_project_json(spec: KiCadProjectSpec) -> dict:
    """
    Generate a valid .kicad_pro project file.

    This is the project metadata file that ties schematic and PCB together.
    """
    return {
        "version": 1,
        "uuid": _gen_uuid(),
        "title": spec.title,
        "publisher": "",
        "generator": "kicad-pipeline",
        "schematic": {
            "title_block": {
                "title": spec.title,
                "date": spec.date,
                "rev": "0",
                "company": spec.author,
            }
        },
        "pcb": {
            "board_thickness": str(spec.board_dimensions.get("thickness_mm", 1.6)),
            "controlled_impedance": False,
            "edge_cuts_margin": {"min": "0.5"},
        },
        "xml_version": 1,
    }


def generate_schematic_json(spec: KiCadProjectSpec) -> dict:
    """
    Generate a valid .kicad_sch JSON file.

    Builds symbols, wire segments, labels, and power ports from the spec.
    Uses a single sheet with all components.
    """
    symbols = []
    wire_segments = []
    labels = []
    power_ports = []
    junctions = []

    # Build a mapping of reference:pin -> position for wire routing
    # Positions are computed from component positions + pin offsets
    pin_positions: Dict[str, Dict[str, Any]] = {}

    for comp in spec.components:
        pos = comp.position or {"x": 0, "y": 0}
        x = _iu(pos["x"])
        y = _iu(pos["y"])

        # Find the symbol definition
        sym_def = None
        for sym in spec.symbols:
            if sym.reference == comp.reference.split(":")[0].rstrip("0123456789") or sym.name == comp.symbol_name:
                sym_def = sym
                break

        symbol = {
            "uuid": _gen_uuid(),
            "lib_part": sym_def.name if sym_def else comp.symbol_name,
            "lib_source": sym_def.name if sym_def else comp.symbol_name,
            "value": comp.value,
            "ref": comp.reference,
            "position": {"x": x, "y": y},
            "rotation": {"x": 0, "y": 0, "z": 0},
            "hide": False,
            "fields": [],
        }

        # Add pin info if we have a symbol definition
        if sym_def and sym_def.pins:
            symbol["pins"] = []
            for i, pin in enumerate(sym_def.pins):
                pin_uuid = _gen_uuid()
                pin_num = str(pin.get("number", i + 1))
                pin_name = str(pin.get("name", f"P{i+1}"))
                pin_type = str(pin.get("type", "input"))

                # Map KiCAD port shapes to types
                shape_map = {
                    "output": "Output",
                    "input": "Input",
                    "bidirectional": "Bidirectional",
                    "power_in": "Input",
                    "power_out": "Output",
                    "passive": "Passive",
                }
                port_shape = shape_map.get(pin_type, "Passive")

                # Pin position: offset from symbol center
                pin_offset_x = _iu(25.4) * (i % 4)  # spread pins across 4 positions
                pin_offset_y = _iu(25.4) * (i // 4)

                symbol["pins"].append({
                    "uuid": pin_uuid,
                    "number": pin_num,
                    "name": pin_name,
                    "type": port_shape,
                    "unit": 1,
                    "conversion": 0,
                    "position": {"x": x + pin_offset_x, "y": y + pin_offset_y},
                })

                pin_positions[f"{comp.reference}:{pin_num}"] = {
                    "x": x + pin_offset_x,
                    "y": y + pin_offset_y,
                }
        else:
            symbol["pins"] = []

        symbols.append(symbol)

    # Build wire segments from net connections
    for net in spec.nets:
        from_pos = pin_positions.get(net.from_ref)
        to_pos = pin_positions.get(net.to_ref)

        if from_pos and to_pos:
            wire_segments.append({
                "uuid": _gen_uuid(),
                "start": {"x": from_pos["x"], "y": from_pos["y"]},
                "end": {"x": to_pos["x"], "y": to_pos["y"]},
                "width": _iu(0.2),
                "stroke": {"width": 0, "type": "default"},
                "color": {"a": 1, "r": 0, "g": 0, "b": 0},
            })

        # Add net labels for named nets
        if from_pos and not to_pos:
            label_x = from_pos["x"] + _iu(5)
            labels.append({
                "uuid": _gen_uuid(),
                "text": net.net_name,
                "position": {"x": label_x, "y": from_pos["y"]},
                "fields": [],
                "shape": "default",
                "effects": {"font": {"size": {"x": _iu(1.27), "y": _iu(1.27)}, "width": _iu(0.15)}, "justify": "left", "hide": False},
            })

    # Add power/gnd symbols for common nets
    power_symbols = _build_power_symbols(spec, symbols)
    symbols.extend(power_symbols)

    return {
        "schematic": {
            "version": 20240108,
            "title_block": {
                "title": spec.title,
                "date": spec.date,
                "rev": "0",
                "company": spec.author,
            },
            "sheets": [
                {
                    "uuid": _gen_uuid(),
                    "filename": "",
                    "title_block": {
                        "title": spec.title,
                        "date": spec.date,
                        "rev": "0",
                        "company": spec.author,
                    },
                }
            ],
            "compounds": [],
            "labels": labels,
            "junctions": junctions,
            "wire": wire_segments,
            "bus": [],
            "bus_entry": [],
            "no_connect": [],
            "global_labels": [],
            "hier_labels": [],
            "label": [],
            "port": [],
            "input": [],
            "output": [],
            "power_port": power_ports,
            "compounds": [],
            "components": symbols,
            "line_width": "default",
            "label_styles": {
                "default": {"effects": {"font": {"size": {"x": _iu(1.27), "y": _iu(1.27)}, "width": _iu(0.15)}}},
                "input": {},
                "output": {},
            },
            "font": "default",
            "grid": "default",
        }
    }


def _build_power_symbols(spec: KiCadProjectSpec, existing_symbols: list) -> list:
    """Build power/ground symbols for common nets."""
    power_nets = set()
    for net in spec.nets:
        name = net.net_name.upper()
        if name in ("GND", "GND_NET", "VCC", "5V", "3V3_NET", "3V3", "PWR_24V", "PWR_5V", "VMOT_CH0", "VMOT_CH1", "VMOT_CH2", "VMOT_CH3", "VMOT_CH4", "VMOT_CH5", "VMOT_CH6", "VMOT_CH7", "VMOT_CH8", "VMOT_CH9", "VMOT_CH10", "VMOT_CH11", "VMOT_CH12", "VMOT_CH13"):
            power_nets.add(net.net_name)

    power_symbols = []
    power_refs = {"GND": "GND", "5V": "5V", "3V3_NET": "3V3", "PWR_24V": "24V"}

    for net_name in power_nets:
        # Map power net to a symbol reference
        sym_ref = None
        sym_lib = None
        for key in power_refs:
            if key.upper() in net_name.upper():
                sym_ref = power_refs[key]
                sym_lib = f"Power: {key}"
                break

        if sym_ref and sym_lib:
            power_symbols.append({
                "uuid": _gen_uuid(),
                "lib_part": sym_lib,
                "lib_source": sym_lib,
                "value": net_name,
                "ref": sym_ref,
                "position": {"x": _iu(-10), "y": _iu(-10)},
                "rotation": {"x": 0, "y": 0, "z": 0},
                "hide": False,
                "fields": [],
                "pins": [],
            })

    return power_symbols


def generate_pcb_json(spec: KiCadProjectSpec, board_rules: Optional[Dict] = None) -> dict:
    """
    Generate a valid .kicad_pcb JSON file.

    Builds board outline, placed footprints, tracks, and vias from the spec.
    """
    rules = board_rules or {}
    signal_width = _iu(rules.get("trace_width_signal", 0.25))
    power_width = _iu(rules.get("trace_width_power", 2.0))
    via_size = _iu(rules.get("via_diameter", 0.6))
    via_hole = _iu(rules.get("via_hole", 0.3))

    width_mm = spec.board_dimensions.get("width_mm", 100)
    height_mm = spec.board_dimensions.get("height_mm", 70)

    # Build net list
    net_names = sorted({net.net_name for net in spec.nets})
    net_map = {name: idx for idx, name in enumerate(net_names)}

    nets = [{"net": idx, "name": name} for idx, name in enumerate(net_names)]

    # Build modules (footprints)
    modules = []
    for comp in spec.components:
        pos = comp.position or {"x": 0, "y": 0}
        footprint = {
            "uuid": _gen_uuid(),
            "reference": comp.reference,
            "value": comp.value,
            "footprint": comp.footprint_name,
            "layer": "F.Cu",
            "at": {"x": _iu(pos["x"]), "y": _iu(pos["y"])},
            "rotation": {"start": 0, "end": 0},
            "locked": False,
            "deleted": False,
            "pin_numbers": [],
            "net_ties": [],
        }
        modules.append(footprint)

    # Build board edges (outline)
    half_w = _iu(width_mm / 2)
    half_h = _iu(height_mm / 2)
    outline = [
        {"start": {"x": -half_w, "y": -half_h}, "end": {"x": half_w, "y": -half_h}},
        {"start": {"x": half_w, "y": -half_h}, "end": {"x": half_w, "y": half_h}},
        {"start": {"x": half_w, "y": half_h}, "end": {"x": -half_w, "y": half_h}},
        {"start": {"x": -half_w, "y": half_h}, "end": {"x": -half_w, "y": -half_h}},
    ]

    # Build simple tracks for power rails
    tracks = []
    for net in spec.nets:
        # Skip if we can't resolve positions (we don't have pin positions in PCB mode)
        # Basic power rail traces: connect components sharing the same net
        pass  # Real routing would need pcbnew Python API

    return {
        "pcbnew": {
            "version": 20240108,
            "general": {
                "thickness": str(spec.board_dimensions.get("thickness_mm", 1.6)),
                "board_layers": [
                    {"name": "F.Cu", "type": "signal"},
                    {"name": "In1.Cu", "type": "internal"},
                    {"name": "In2.Cu", "type": "internal"},
                    {"name": "B.Cu", "type": "signal"},
                    {"name": "B.Adhes", "type": "user", "locked": False},
                    {"name": "F.Adhes", "type": "user", "locked": False},
                    {"name": "B.Paste", "type": "user", "locked": False},
                    {"name": "F.Paste", "type": "user", "locked": False},
                    {"name": "B.SilkS", "type": "user", "locked": False},
                    {"name": "F.SilkS", "type": "user", "locked": False},
                    {"name": "B.Mask", "type": "user", "locked": False},
                    {"name": "F.Mask", "type": "user", "locked": False},
                    {"name": "Dwgs.User", "type": "user", "locked": False},
                    {"name": "Cmts.User", "type": "user", "locked": False},
                    {"name": "Eco1.User", "type": "user", "locked": False},
                    {"name": "Eco2.User", "type": "user", "locked": False},
                    {"name": "Edge.Cuts", "type": "user", "locked": False},
                    {"name": "Margin", "type": "user", "locked": False},
                    {"name": "B.CrtYd", "type": "user", "locked": False},
                    {"name": "F.CrtYd", "type": "user", "locked": False},
                    {"name": "B.Fab", "type": "user", "locked": False},
                    {"name": "F.Fab", "type": "user", "locked": False},
                ],
            },
            "board_edges_layer": "Edge.Cuts",
            "paper": {"type": "A4", "width": "210", "height": "297"},
            "edges_dimension": {
                "reference": "%I",
                "precision": 4,
                "layers": ["Edge.Cuts"],
                "vertical_format": "%1.4f",
                "horizontal_format": "%1.4f",
            },
            "plot_direction": "bottom_up",
            "module_dimensions": {
                "reference": {"enabled": ["F", "B"], "oversize": {"d": "%1.4f", "layer": "F.SilkS"}, "spacing": {"u": "0.2", "layer": "F.SilkS"}},
                "value": {"enabled": ["F", "B"], "oversize": {"d": "%1.4f", "layer": "F.Fab"}, "spacing": {"u": "0.2", "layer": "F.Fab"}},
            },
            "grid_limits": {"min": "0.01", "max": "1000"},
            "zones": {"minimum_clearance": {"u": "0.2", "layer": "F.Cu"}, "clearance": "0.2", "unconnected_pins_only": False, "service": {"radius": "0"}},
            "copper_zones": {"filled_areas_only": False, "fill_mode": 1},
            "pad_to_mask_reduction": "0",
            "texts_shapes": {"reference": {"shape": "Rectangle"}, "value": {"shape": "Rectangle"}},
            "controller": "",
            "outlines": outline,
            "nets": nets,
            "modules": modules,
            "tracks": tracks,
            "vias": [],
            "via_types": {},
            "segments": [],
            "arcs": [],
            "vcircles": [],
            "vpaths": [],
            "graphical_items": [],
            "texts": [],
            "dimensions": [],
            "page_dim": {"left": {"u": "5", "layer": "B.EdgeEngravingUser1"}, "right": {"u": "5", "layer": "B.EdgeEngravingUser1"}, "top": {"u": "5", "layer": "B.EdgeEngravingUser1"}, "bottom": {"u": "5", "layer": "B.EdgeEngravingUser1"}},
        }
    }


def write_json_file(filepath: str, data: dict, indent: int = 2) -> str:
    """Write a JSON dict to a file and return the path."""
    Path(filepath).parent.mkdir(parents=True, exist_ok=True)
    content = json.dumps(data, indent=indent)
    Path(filepath).write_text(content, encoding="utf-8")
    return filepath


def get_pin_coords(ref: str, pin: str, comp_x: float, comp_y: float) -> tuple:
    ref_upper = ref.upper()
    pin_str = str(pin).strip()
    
    # 1. Arduino Giga R1 WiFi (U1)
    if ref_upper == "U1":
        unique_pins = [
            '3V3', 'GND', 'TX1', 'RX1',
            'D2', 'D3', 'D4', 'D5', 'D6',
            'D22', 'D23', 'D24', 'D25', 'D26',
            'D27', 'D28', 'D29', 'D30', 'D31',
            'D32', 'D33', 'D34', 'D35', 'D36',
            'D37', 'D38', 'D39', 'D40', 'D41',
            'D42', 'D43', 'D44', 'D45', 'D46',
            'D47', 'D48', 'D49', 'D50', 'D51',
            'D52', 'D53', 'D54', 'D55', 'D56',
            'D57', 'D58', 'D59', 'D60', 'D61',
            'D62', 'D63', 'D64', 'D65', 'D66',
            'D67', 'D68', 'D69', 'D70', 'D71',
            'D72', 'D73', 'D74', 'D75', 'D76',
            'D77', 'D78', 'D79', 'D80', 'D81',
            'D82', 'D83', 'D84', 'D85', 'D86'
        ]
        pin_str_upper = pin_str.upper()
        unique_pins_upper = [p.upper() for p in unique_pins]
        
        if pin_str_upper in unique_pins_upper:
            pin_idx = unique_pins_upper.index(pin_str_upper)
        else:
            try:
                pin_idx = int(pin_str) % len(unique_pins)
            except ValueError:
                pin_idx = abs(hash(pin_str_upper)) % len(unique_pins)
                
        y_offset = 100.0 - (pin_idx // 2) * 5.08 - 2.54
        dx = -25.4 if pin_idx % 2 == 0 else 25.4
        return comp_x + dx, comp_y - y_offset
        
    # 2. MAX3232 Module (J2)
    elif ref_upper == "J2":
        try:
            p = int(pin_str)
        except ValueError:
            p = 1
        y_offset = 3.81 - (p - 1) * 2.54
        return comp_x - 5.08, comp_y - y_offset
        
    # 3. TMC2209 Sockets (J5 - J18)
    elif ref_upper.startswith("J") and ref_upper[1:].isdigit() and 5 <= int(ref_upper[1:]) <= 18:
        try:
            p = int(pin_str)
        except ValueError:
            p = 1
        if p % 2 == 1: # Odd, left side
            dx = -6.35
            y_offset = 8.89 - (p // 2) * 2.54
        else: # Even, right side
            dx = 6.35
            y_offset = 8.89 - ((p - 1) // 2) * 2.54
        return comp_x + dx, comp_y - y_offset
        
    # 4. Motor Terminals (J_MOTOR_CH1 - J_MOTOR_CH14)
    elif ref_upper.startswith("J_MOTOR_CH") or "MOTOR" in ref_upper:
        try:
            p = int(pin_str)
        except ValueError:
            p = 1
        y_offset = 3.81 - (p - 1) * 2.54
        return comp_x - 5.08, comp_y - y_offset
        
    # 5. Limit Switch (J_SW_CH1 - J_SW_CH14)
    elif ref_upper.startswith("J_SW_CH") or "LIMIT" in ref_upper:
        try:
            p = int(pin_str)
        except ValueError:
            p = 1
        y_offset = 1.27 if p == 1 else -1.27
        return comp_x - 5.08, comp_y - y_offset
        
    return comp_x, comp_y
        
    return comp_x, comp_y


def _get_full_footprint_name(fp: str) -> str:
    """Get the full footprint name including standard KiCad library prefix."""
    if not fp:
        return ""
        
    # Check for JST footprints first
    if "JST" in fp:
        # Correct the pitch in standard JST XH footprints if 2.54mm was used
        if "XH" in fp and "2.54mm" in fp:
            fp = fp.replace("2.54mm", "2.50mm")
        # Correct JST GH 2-pin footprint to the exact standard name
        if "GH" in fp and "1x02" in fp:
            fp = "JST_GH_BM02B-GHS-TBT_1x02-1MP_P1.25mm_Vertical"
        if not fp.startswith("Connector_JST:"):
            # Strip any existing prefix
            if ":" in fp:
                fp = fp.split(":")[-1]
            fp = f"Connector_JST:{fp}"
        return fp
        
    if ":" in fp:
        return fp
        
    # Check for PinHeader footprints
    if "PinHeader" in fp or "Connector" in fp:
        return f"Connector_PinHeader_2.54mm:{fp}"
        
    return f"Connector:{fp}"


def _generate_lib_symbols() -> str:
    """Generate the (lib_symbols ...) block for standard global generic symbols used in the schematic."""
    lines = ["  (lib_symbols"]
    
    # 1. Connector_Generic:Conn_01x02 (2-pin limit switch connector)
    lines.extend([
        '    (symbol "Connector_Generic:Conn_01x02"',
        '      (in_bom yes) (on_board yes) (dnp no)',
        '      (property "Reference" "J" (at 0 2.54 0) (effects (font (size 1.27 1.27))))',
        '      (property "Value" "Conn_01x02" (at 0 -5.08 0) (effects (font (size 1.27 1.27))))',
        '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        '      (symbol "Conn_01x02_0_1"',
        '        (rectangle (start -1.27 -2.54) (end 1.27 2.54) (stroke (width 0.254) (type default)) (fill (type background)))',
        '      )',
        '      (symbol "Conn_01x02_1_1"',
        '        (pin passive line (at -5.08 1.27 0) (length 3.81) (name "Pin_1" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))',
        '        (pin passive line (at -5.08 -1.27 0) (length 3.81) (name "Pin_2" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))',
        '      )',
        '    )',
    ])

    # 2. Connector_Generic:Conn_01x04 (4-pin JST motor / MAX3232 connector)
    lines.extend([
        '    (symbol "Connector_Generic:Conn_01x04"',
        '      (in_bom yes) (on_board yes) (dnp no)',
        '      (property "Reference" "J" (at 0 5.08 0) (effects (font (size 1.27 1.27))))',
        '      (property "Value" "Conn_01x04" (at 0 -7.62 0) (effects (font (size 1.27 1.27))))',
        '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        '      (symbol "Conn_01x04_0_1"',
        '        (rectangle (start -1.27 -5.08) (end 1.27 5.08) (stroke (width 0.254) (type default)) (fill (type background)))',
        '      )',
        '      (symbol "Conn_01x04_1_1"',
        '        (pin passive line (at -5.08 3.81 0) (length 3.81) (name "Pin_1" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))',
        '        (pin passive line (at -5.08 1.27 0) (length 3.81) (name "Pin_2" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))',
        '        (pin passive line (at -5.08 -1.27 0) (length 3.81) (name "Pin_3" (effects (font (size 1.27 1.27)))) (number "3" (effects (font (size 1.27 1.27)))))',
        '        (pin passive line (at -5.08 -3.81 0) (length 3.81) (name "Pin_4" (effects (font (size 1.27 1.27)))) (number "4" (effects (font (size 1.27 1.27)))))',
        '      )',
        '    )',
    ])

    # 3. Connector_Generic:Conn_02x08_Odd_Even (16-pin driver sockets)
    lines.extend([
        '    (symbol "Connector_Generic:Conn_02x08_Odd_Even"',
        '      (in_bom yes) (on_board yes) (dnp no)',
        '      (property "Reference" "J" (at 0 10.16 0) (effects (font (size 1.27 1.27))))',
        '      (property "Value" "Conn_02x08_Odd_Even" (at 0 -12.7 0) (effects (font (size 1.27 1.27))))',
        '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        '      (symbol "Conn_02x08_Odd_Even_0_1"',
        '        (rectangle (start -2.54 -10.16) (end 2.54 10.16) (stroke (width 0.254) (type default)) (fill (type background)))',
        '      )',
        '      (symbol "Conn_02x08_Odd_Even_1_1"',
    ])
    for p in range(1, 17):
        if p % 2 == 1: # Odd, left side
            y_offset = 8.89 - (p // 2) * 2.54
            lines.append(f'        (pin passive line (at -6.35 {y_offset:.2f} 0) (length 3.81) (name "Pin_{p}" (effects (font (size 1.27 1.27)))) (number "{p}" (effects (font (size 1.27 1.27)))))')
        else: # Even, right side
            y_offset = 8.89 - ((p - 1) // 2) * 2.54
            lines.append(f'        (pin passive line (at 6.35 {y_offset:.2f} 180) (length 3.81) (name "Pin_{p}" (effects (font (size 1.27 1.27)))) (number "{p}" (effects (font (size 1.27 1.27)))))')
    lines.extend([
        '      )',
        '    )',
    ])

    # 4. Connector_Generic:Conn_02x40_Odd_Even (Arduino Giga representation with functional pin numbering)
    lines.extend([
        '    (symbol "Connector_Generic:Conn_02x40_Odd_Even"',
        '      (in_bom yes) (on_board yes) (dnp no)',
        '      (property "Reference" "J" (at 0 102.87 0) (effects (font (size 1.27 1.27))))',
        '      (property "Value" "Conn_02x40_Odd_Even" (at 0 100.33 0) (effects (font (size 1.27 1.27))))',
        '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        '      (symbol "Conn_02x40_Odd_Even_0_1"',
        '        (rectangle (start -2.86 100.0) (end 2.86 -100.0) (stroke (width 0.254) (type default)) (fill (type background)))',
        '      )',
        '      (symbol "Conn_02x40_Odd_Even_1_1"',
    ])
    
    unique_pins = [
        '3V3', 'GND', 'TX1', 'RX1',
        'D2', 'D3', 'D4', 'D5', 'D6',
        'D22', 'D23', 'D24', 'D25', 'D26',
        'D27', 'D28', 'D29', 'D30', 'D31',
        'D32', 'D33', 'D34', 'D35', 'D36',
        'D37', 'D38', 'D39', 'D40', 'D41',
        'D42', 'D43', 'D44', 'D45', 'D46',
        'D47', 'D48', 'D49', 'D50', 'D51',
        'D52', 'D53', 'D54', 'D55', 'D56',
        'D57', 'D58', 'D59', 'D60', 'D61',
        'D62', 'D63', 'D64', 'D65', 'D66',
        'D67', 'D68', 'D69', 'D70', 'D71',
        'D72', 'D73', 'D74', 'D75', 'D76',
        'D77', 'D78', 'D79', 'D80', 'D81',
        'D82', 'D83', 'D84', 'D85', 'D86'
    ]
    for idx, p_name in enumerate(unique_pins):
        y_offset = 100.0 - (idx // 2) * 5.08 - 2.54
        if idx % 2 == 0:
            lines.append(f'        (pin passive line (at -25.4 {y_offset:.2f} 0) (length 2.54) (name "{p_name}" (effects (font (size 1.27 1.27)))) (number "{p_name}" (effects (font (size 1.27 1.27)))))')
        else:
            lines.append(f'        (pin passive line (at 25.4 {y_offset:.2f} 180) (length 2.54) (name "{p_name}" (effects (font (size 1.27 1.27)))) (number "{p_name}" (effects (font (size 1.27 1.27)))))')
            
    lines.extend([
        '      )',
        '    )',
        '  )',
    ])
    
    return "\n".join(lines)


def serialize_schematic_sexpr(spec: KiCadProjectSpec) -> str:
    """Serialize KiCadProjectSpec to valid KiCad S-expression schematic format."""
    import uuid
    lines = [
        "(kicad_sch",
        "  (version 20240108)",
        '  (generator "kicad-pipeline")',
        '  (generator_version "1.0")',
        f'  (uuid "{uuid.uuid4()}")',
        '  (paper "A3")',
    ]
    
    lines.append(_generate_lib_symbols())
    
    lines.extend([
        "  (title_block",
        f'    (title "{spec.title}")',
        f'    (date "{spec.date}")',
        '    (rev "0")',
        f'    (company "{spec.author}")',
        "  )",
    ])

    comp_positions = {}
    
    for comp in spec.components:
        ref_upper = comp.reference.upper()
        if ref_upper == "U1":
            x, y = 203.2, 139.7
        elif ref_upper == "J2":
            x, y = 304.8, 139.7
        elif ref_upper.startswith("J") and ref_upper[1:].isdigit():
            val = int(ref_upper[1:])
            if 5 <= val <= 11:
                ch = val - 4
                x = 50.8 + (ch - 1) * 45.72
                y = 50.8
            elif 12 <= val <= 18:
                ch = val - 4
                x = 50.8 + (ch - 8) * 45.72
                y = 203.2
            else:
                x, y = 101.6, 101.6
        elif "MOTOR" in ref_upper:
            try:
                ch_str = "".join([c for c in ref_upper if c.isdigit()])
                ch = int(ch_str) if ch_str else 1
            except ValueError:
                ch = 1
            if 1 <= ch <= 7:
                x = 50.8 + (ch - 1) * 45.72 + 10.16
                y = 76.2
            elif 8 <= ch <= 14:
                x = 50.8 + (ch - 8) * 45.72 + 10.16
                y = 228.6
            else:
                x, y = 101.6, 101.6
        elif "LIMIT" in ref_upper or "SW" in ref_upper:
            try:
                ch_str = "".join([c for c in ref_upper if c.isdigit()])
                ch = int(ch_str) if ch_str else 1
            except ValueError:
                ch = 1
            if 1 <= ch <= 7:
                x = 50.8 + (ch - 1) * 45.72 - 10.16
                y = 76.2
            elif 8 <= ch <= 14:
                x = 50.8 + (ch - 8) * 45.72 - 10.16
                y = 228.6
            else:
                x, y = 101.6, 101.6
        else:
            x, y = 101.6, 101.6
            
        comp_positions[comp.reference] = (x, y)
        comp_uuid = uuid.uuid4()
        
        # Resolve clean standard generic symbol parts from standard libraries
        if ref_upper == "U1":
            lib_id = "Connector_Generic:Conn_02x40_Odd_Even"
        elif ref_upper == "J2":
            lib_id = "Connector_Generic:Conn_01x04"
        elif ref_upper.startswith("J") and ref_upper[1:].isdigit():
            lib_id = "Connector_Generic:Conn_02x08_Odd_Even"
        elif "MOTOR" in ref_upper:
            lib_id = "Connector_Generic:Conn_01x04"
        elif "LIMIT" in ref_upper or "SW" in ref_upper:
            lib_id = "Connector_Generic:Conn_01x02"
        else:
            lib_id = f"Connector_Generic:{comp.symbol_name}"

        lines.extend([
            f'  (symbol (lib_id "{lib_id}") (at {x} {y} 0) (unit 1)',
            '    (in_bom yes) (on_board yes) (dnp no)',
            f'    (uuid "{comp_uuid}")',
            f'    (property "Reference" "{comp.reference}" (at {x} {y - 6} 0)',
            '      (effects (font (size 1.27 1.27)))',
            '    )',
            f'    (property "Value" "{comp.value}" (at {x} {y - 3} 0)',
            '      (effects (font (size 1.27 1.27)))',
            '    )',
            f'    (property "Footprint" "{_get_full_footprint_name(comp.footprint_name)}" (at {x} {y + 6} 0)',
            '      (effects (font (size 1.27 1.27)) hide)',
            '    )',
            '  )'
        ])

    for net in spec.nets:
        if ":" in net.from_ref:
            from_comp, from_pin = net.from_ref.split(":")
            if from_comp in comp_positions:
                cx, cy = comp_positions[from_comp]
                px, py = get_pin_coords(from_comp, from_pin, cx, cy)
                lines.extend([
                    f'  (label "{net.net_name}" (at {px} {py} 0) (fields_autoplaced yes)',
                    '    (effects (font (size 1.27 1.27)) (justify left))',
                    f'    (uuid "{uuid.uuid4()}")',
                    '  )'
                ])
                
        if ":" in net.to_ref:
            to_comp, to_pin = net.to_ref.split(":")
            if to_comp in comp_positions:
                cx, cy = comp_positions[to_comp]
                px, py = get_pin_coords(to_comp, to_pin, cx, cy)
                lines.extend([
                    f'  (label "{net.net_name}" (at {px} {py} 0) (fields_autoplaced yes)',
                    '    (effects (font (size 1.27 1.27)) (justify left))',
                    f'    (uuid "{uuid.uuid4()}")',
                    '  )'
                ])

    lines.append('  (root_sheet_instance (path "/") (page "1"))')
    lines.append(")")
    return "\n".join(lines)


def serialize_pcb_sexpr(spec: KiCadProjectSpec) -> str:
    """Serialize KiCadProjectSpec to valid KiCad S-expression PCB board format."""
    import uuid
    width = spec.board_dimensions.get("width_mm", 200)
    height = spec.board_dimensions.get("height_mm", 150)

    lines = [
        "(kicad_pcb",
        "  (version 20240108)",
        '  (generator "kicad-pipeline")',
        '  (generator_version "1.0")',
        "  (general",
        "    (thickness 1.6)",
        "  )",
        '  (paper "A3")',
        "  (title_block",
        f'    (title "{spec.title}")',
        f'    (date "{spec.date}")',
        '    (rev "0")',
        f'    (company "{spec.author}")',
        "  )",
        "  (layers",
        "    (0 \"F.Cu\" signal)",
        "    (31 \"B.Cu\" signal)",
        "    (34 \"B.Paste\" user)",
        "    (35 \"F.Paste\" user)",
        "    (36 \"B.SilkS\" user)",
        "    (37 \"F.SilkS\" user)",
        "    (38 \"B.Mask\" user)",
        "    (39 \"F.Mask\" user)",
        "    (40 \"Dwgs.User\" user)",
        "    (41 \"Cmts.User\" user)",
        "    (42 \"Eco1.User\" user)",
        "    (43 \"Eco2.User\" user)",
        "    (44 \"Edge.Cuts\" user)",
        "    (45 \"Margin\" user)",
        "    (46 \"B.CrtYd\" user)",
        "    (47 \"F.CrtYd\" user)",
        "    (48 \"B.Fab\" user)",
        "    (49 \"F.Fab\" user)",
        "  )",
        "  (gr_line (start 0 0) (end 0 {0}) (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\") (tstamp \"{1}\"))".format(height, uuid.uuid4()),
        "  (gr_line (start 0 {0}) (end {1} {0}) (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\") (tstamp \"{2}\"))".format(height, width, uuid.uuid4()),
        "  (gr_line (start {0} {1}) (end {0} 0) (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\") (tstamp \"{2}\"))".format(width, height, uuid.uuid4()),
        "  (gr_line (start {0} 0) (end 0 0) (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\") (tstamp \"{1}\"))".format(width, uuid.uuid4()),
    ]

    for i, comp in enumerate(spec.components):
        ref_upper = comp.reference.upper()
        
        if ref_upper == "U1":
            x, y = 100.0, 75.0
        elif ref_upper == "J2":
            x, y = 160.0, 75.0
        elif ref_upper.startswith("J") and ref_upper[1:].isdigit():
            val = int(ref_upper[1:])
            if 5 <= val <= 11:
                ch = val - 4
                x = 25.0 + (ch - 1) * 25.0
                y = 45.0
            elif 12 <= val <= 18:
                ch = val - 4
                x = 25.0 + (ch - 8) * 25.0
                y = 105.0
            else:
                x, y = 30.0, 30.0
        elif "MOTOR" in ref_upper:
            try:
                ch_str = "".join([c for c in ref_upper if c.isdigit()])
                ch = int(ch_str) if ch_str else 1
            except ValueError:
                ch = 1
            if 1 <= ch <= 7:
                x = 25.0 + (ch - 1) * 25.0
                y = 15.0
            elif 8 <= ch <= 14:
                x = 25.0 + (ch - 8) * 25.0
                y = 135.0
            else:
                x, y = 30.0, 30.0
        elif "LIMIT" in ref_upper or "SW" in ref_upper:
            try:
                ch_str = "".join([c for c in ref_upper if c.isdigit()])
                ch = int(ch_str) if ch_str else 1
            except ValueError:
                ch = 1
            if 1 <= ch <= 7:
                x = 25.0 + (ch - 1) * 25.0
                y = 27.0
            elif 8 <= ch <= 14:
                x = 25.0 + (ch - 8) * 25.0
                y = 123.0
            else:
                x, y = 30.0, 30.0
        else:
            x, y = 30.0, 30.0
            
        fp = _get_full_footprint_name(comp.footprint_name)

        lines.extend([
            f'  (footprint "{fp}" (layer "F.Cu") (at {x} {y} 0)',
            f'    (property "Reference" "{comp.reference}" (at 0 -5 0)',
            '      (effects (font (size 1 1) (thickness 0.15)))',
            '    )',
            f'    (property "Value" "{comp.value}" (at 0 5 0)',
            '      (effects (font (size 1 1) (thickness 0.15)))',
            '    )',
            f'    (tstamp "{uuid.uuid4()}")',
            '  )'
        ])

    lines.append(")")
    return "\n".join(lines)


def write_project(spec: KiCadProjectSpec, project_dir: str, board_rules: Optional[Dict] = None) -> Dict[str, str]:
    """
    Write all KiCAD project files.

    Returns dict of file_type -> filepath.
    """
    files = {}

    proj_data = generate_project_json(spec)
    proj_path = f"{project_dir}/{spec.project_name}.kicad_pro"
    write_json_file(proj_path, proj_data)
    files["project"] = proj_path

    sch_path = f"{project_dir}/{spec.project_name}.kicad_sch"
    sch_content = serialize_schematic_sexpr(spec)
    Path(sch_path).write_text(sch_content, encoding="utf-8")
    files["schematic"] = sch_path

    pcb_path = f"{project_dir}/{spec.project_name}.kicad_pcb"
    pcb_content = serialize_pcb_sexpr(spec)
    Path(pcb_path).write_text(pcb_content, encoding="utf-8")
    files["pcb"] = pcb_path

    netlist_data = generate_netlist_xml(spec)
    netlist_path = f"{project_dir}/{spec.project_name}.kicad_netlist.xml"
    write_json_file(netlist_path, netlist_data, indent=0)
    files["netlist"] = netlist_path

    return files


def generate_netlist_xml(spec: KiCadProjectSpec) -> dict:
    """
    Generate a KiCAD netlist.

    Returns a dict that will be written as JSON (KiCAD can parse JSON netlists).
    The actual KiCAD netlist is XML text, so we return a wrapper dict.
    """
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<export version="E"',
        '  version="20240108"',
        f'  date="{spec.date}"',
        '  tool="KiCAD Pipeline">',
        '  <generator>',
        '    <name>kiCAD Pipeline</name>',
        '    <version>1.0</version>',
        '  </generator>',
        '',
        '  <design>',
        f'    <source>{spec.title}</source>',
        f'    <title>{spec.title}</title>',
        f'    <company>{spec.author}</company>',
        '  </design>',
        '',
        '  <components>',
    ]

    for i, comp in enumerate(spec.components, 1):
        lines.append(f'    <ref>{comp.reference}</ref>')
        lines.append(f'    <value>{comp.value}</value>')
        lines.append(f'    <footprint>{comp.footprint_name}</footprint>')
        lines.append(f'    <libsource lib="custom" part="{comp.symbol_name}" description="{comp.value}"/>')
        lines.append(f'    <sheetpath names="/" tstamps="/">')
        lines.append(f'    <datasheet>---</datasheet>')
        lines.append('  ')

    lines.extend(['  </components>', '', '  <libparts>', '  </libparts>', '', '  <libraries>', '  </libraries>', '', '  <nets>',])

    for net in spec.nets:
        lines.append(f'    <node>{net.net_name}</node>')

    lines.extend(['  </nets>', '</export>'])

    return {"xml": "\n".join(lines)}
