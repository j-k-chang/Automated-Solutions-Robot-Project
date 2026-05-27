# generate_14ch_spec_modular.py
import json
from datetime import datetime

def generate_spec():
    spec = {
        "project": {
            "name": "14ch_dispensing_system",
            "description": "14-channel gravimetric dispensing system with Arduino Giga R1 WiFi, plug-in BIGTREETECH TMC2209 modules, plug-in HiLetgo MAX3232 RS232 converter, and JST-XH pump connections.",
            "version": "2.0",
            "date": datetime.now().strftime("%Y-%m-%d"),
            "author": "Jimmy Chang"
        },
        "schematic": {
            "components": [],
            "connections": []
        },
        "pcb": {
            "board_size": {
                "width": 200,
                "height": 150,
                "unit": "mm"
            },
            "layers": 4,
            "trace_width": {
                "power": 2.0,
                "signal": 0.25,
                "ground": 0.5
            },
            "clearance": 0.2,
            "via_size": 0.6,
            "via_hole": 0.3,
            "placement": {
                "arduino": {"x": 100, "y": 75, "rotation": 0},
                "max3232": {"x": 150, "y": 75, "rotation": 0},
                "tmc_modules": [],
                "motor_connectors": [],
                "limit_switch_connectors": []
            },
            "bom": []
        }
    }

    # 1. Add Microcontroller
    spec["schematic"]["components"].append({
        "ref": "U1",
        "value": "ARDUINO_GIGA_R1_WIFI",
        "footprint": "Arduino_Giga_R1_WiFi_Board",
        "libsource": {
            "lib": "Interface_Module",
            "part": "Arduino_Giga_R1_WiFi"
        },
        "fields": {
            "Manufacturer": "Arduino",
            "MPN": "ABV00003",
            "Description": "Arduino Giga R1 WiFi with STM32H747 MCU"
        }
    })
    spec["pcb"]["bom"].append({
        "ref": "U1",
        "value": "ARDUINO_GIGA_R1_WIFI",
        "qty": 1,
        "description": "Main controller board (plugged into PCB via female headers)"
    })

    # 2. Add HiLetgo MAX3232 Module
    spec["schematic"]["components"].append({
        "ref": "J2",
        "value": "MAX3232_MODULE",
        "footprint": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
        "libsource": {
            "lib": "Connector",
            "part": "Connector_01x04"
        },
        "fields": {
            "Manufacturer": "HiLetgo / Various",
            "MPN": "B00LPK0Z9A",
            "Description": "RS232 to TTL Converter Module (socketed on female header)"
        }
    })
    spec["pcb"]["bom"].append({
        "ref": "J2",
        "value": "MAX3232_MODULE",
        "qty": 1,
        "description": "HiLetgo MAX3232 RS232 to TTL Converter Module (plug-in)"
    })

    # 3. Add TMC2209 Drivers, Motor Connectors, Limit Switch Connectors for 14 channels
    ch_pin_groups = [
        # ch: (step, dir, en, ms1, ms2)
        (1, "D2", "D3", "D4", "D5", "D6"),
        (2, "D22", "D23", "D24", "D25", "D26"),
        (3, "D27", "D28", "D29", "D30", "D31"),
        (4, "D32", "D33", "D34", "D35", "D36"),
        (5, "D37", "D38", "D39", "D40", "D41"),
        (6, "D42", "D43", "D44", "D45", "D46"),
        (7, "D47", "D48", "D49", "D50", "D51"),
        (8, "D52", "D53", "D54", "D55", "D56"),
        (9, "D57", "D58", "D59", "D60", "D61"),
        (10, "D62", "D63", "D64", "D65", "D66"),
        (11, "D67", "D68", "D69", "D70", "D71"),
        (12, "D72", "D73", "D74", "D75", "D76"),
        (13, "D77", "D78", "D79", "D80", "D81"),
        (14, "D82", "D83", "D84", "D85", "D86")
    ]

    for ch, step, dir_pin, en, ms1, ms2 in ch_pin_groups:
        driver_ref = f"J{ch + 4}" # Drivers are J5 to J18
        motor_ref = f"J_MOTOR_CH{ch}"
        sw_ref = f"J_SW_CH{ch}"

        # Add Driver Socket to schematic
        spec["schematic"]["components"].append({
            "ref": driver_ref,
            "value": f"TMC2209_CH{ch}",
            "footprint": "Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Vertical",
            "libsource": {
                "lib": "Connector",
                "part": "Connector_02x08"
            },
            "fields": {
                "Description": f"BIGTREETECH TMC2209 Stepper Driver Module Socket (Channel {ch})"
            }
        })

        # Add Motor Connector (JST-XH 4-pin) to schematic
        spec["schematic"]["components"].append({
            "ref": motor_ref,
            "value": f"MOTOR_TERM_CH{ch}",
            "footprint": "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.54mm_Vertical",
            "libsource": {
                "lib": "Connector",
                "part": "Connector_01x04"
            },
            "fields": {
                "Description": f"4-pin JST-XH connector for Kamoer KPHM100 stepper pump (Channel {ch})"
            }
        })

        # Add Limit Switch Connector (JST-GH 2-pin) to schematic
        spec["schematic"]["components"].append({
            "ref": sw_ref,
            "value": f"LIMIT_SW_CH{ch}",
            "footprint": "Connector_JST:JST_GH_1x02_P1.25mm_Vertical",
            "libsource": {
                "lib": "Connector",
                "part": "Connector_01x02"
            },
            "fields": {
                "Description": f"2-pin JST-GH connector for limit switch (Channel {ch})"
            }
        })

        # Add PCB Placements
        # Row 1 for CH1-9, Row 2 for CH10-14
        if ch <= 9:
            spec["pcb"]["placement"]["tmc_modules"].append({"ref": driver_ref, "x": 20 * ch, "y": 25})
            spec["pcb"]["placement"]["motor_connectors"].append({"ref": motor_ref, "x": 20 * ch, "y": 10})
            spec["pcb"]["placement"]["limit_switch_connectors"].append({"ref": sw_ref, "x": 20 * ch + 10, "y": 10})
        else:
            spec["pcb"]["placement"]["tmc_modules"].append({"ref": driver_ref, "x": 20 * (ch - 9), "y": 55})
            spec["pcb"]["placement"]["motor_connectors"].append({"ref": motor_ref, "x": 20 * (ch - 9), "y": 40})
            spec["pcb"]["placement"]["limit_switch_connectors"].append({"ref": sw_ref, "x": 20 * (ch - 9) + 10, "y": 40})

    spec["pcb"]["bom"].append({
        "ref": "J5-J18",
        "value": "TMC2209_SOCKET",
        "qty": 14,
        "description": "BIGTREETECH TMC2209 Stepper Driver socket (dual 1x8 female header)"
    })
    spec["pcb"]["bom"].append({
        "ref": "J_MOTOR_CH1-CH14",
        "value": "JST-XH-4PIN",
        "qty": 14,
        "description": "4-pin JST-XH vertical header for pump connection"
    })
    spec["pcb"]["bom"].append({
        "ref": "J_SW_CH1-CH14",
        "value": "JST-GH-2PIN",
        "qty": 14,
        "description": "2-pin JST-GH vertical header for limit switch"
    })

    # 4. Add Global Schematic Connections
    # 3.3V Logic Power (safe logic level for Arduino Giga)
    vcc_nodes = [["U1", "3V3"], ["J2", "VCC"]]
    for ch in range(1, 15):
        driver_ref = f"J{ch + 4}"
        vcc_nodes.append([driver_ref, "VDD"]) # TMC2209 Logic power
    
    spec["schematic"]["connections"].append({
        "net": "3V3",
        "nodes": vcc_nodes
    })

    # GND Logic & Power Ground
    gnd_nodes = [["U1", "GND"], ["J2", "GND"]]
    for ch in range(1, 15):
        driver_ref = f"J{ch + 4}"
        gnd_nodes.append([driver_ref, "GND"]) # Driver Logic & Motor grounds share plane
    
    spec["schematic"]["connections"].append({
        "net": "GND",
        "nodes": gnd_nodes
    })

    # 24V Main Motor Power
    vm_nodes = []
    for ch in range(1, 15):
        driver_ref = f"J{ch + 4}"
        vm_nodes.append([driver_ref, "VM"])
    spec["schematic"]["connections"].append({
        "net": "24V",
        "nodes": vm_nodes
    })

    # RS232 UART1 Connections
    spec["schematic"]["connections"].append({
        "net": "RS232_TX",
        "nodes": [["U1", "TX1"], ["J2", "TX"]]
    })
    spec["schematic"]["connections"].append({
        "net": "RS232_RX",
        "nodes": [["U1", "RX1"], ["J2", "RX"]]
    })

    # 5. Add Per-Channel Control & Motor Connections
    for ch, step, dir_pin, en, ms1, ms2 in ch_pin_groups:
        driver_ref = f"J{ch + 4}"
        motor_ref = f"J_MOTOR_CH{ch}"
        sw_ref = f"J_SW_CH{ch}"

        # Control Signals
        spec["schematic"]["connections"].append({
            "net": f"STEP_CH{ch}",
            "nodes": [[driver_ref, "STEP"], ["U1", step]]
        })
        spec["schematic"]["connections"].append({
            "net": f"DIR_CH{ch}",
            "nodes": [[driver_ref, "DIR"], ["U1", dir_pin]]
        })
        spec["schematic"]["connections"].append({
            "net": f"EN_CH{ch}",
            "nodes": [[driver_ref, "EN"], ["U1", en]]
        })
        spec["schematic"]["connections"].append({
            "net": f"MS1_CH{ch}",
            "nodes": [[driver_ref, "MS1"], ["U1", ms1]]
        })
        spec["schematic"]["connections"].append({
            "net": f"MS2_CH{ch}",
            "nodes": [[driver_ref, "MS2"], ["U1", ms2]]
        })

        # Stepper Motor Coil Outputs
        # TMC2209 Phase B (1B, 1A) and Phase A (2A, 2B) connect to JST pins 1, 2, 3, 4
        spec["schematic"]["connections"].append({
            "net": f"MOTOR_A1_CH{ch}",
            "nodes": [[driver_ref, "1B"], [motor_ref, "1"]]
        })
        spec["schematic"]["connections"].append({
            "net": f"MOTOR_A2_CH{ch}",
            "nodes": [[driver_ref, "1A"], [motor_ref, "2"]]
        })
        spec["schematic"]["connections"].append({
            "net": f"MOTOR_B1_CH{ch}",
            "nodes": [[driver_ref, "2A"], [motor_ref, "3"]]
        })
        spec["schematic"]["connections"].append({
            "net": f"MOTOR_B2_CH{ch}",
            "nodes": [[driver_ref, "2B"], [motor_ref, "4"]]
        })

        # Limit Switch Connections
        spec["schematic"]["connections"].append({
            "net": f"LIMIT_SW_CH{ch}",
            "nodes": [[driver_ref, "SW"], [sw_ref, "1"]]
        })
        spec["schematic"]["connections"].append({
            "net": "GND",
            "nodes": [[sw_ref, "2"]]
        })

    # Save to kicad_14ch_dispensing.json
    output_path = "kicad_14ch_dispensing.json"
    with open(output_path, "w") as f:
        json.dump(spec, f, indent=2)
    print(f"Successfully generated {output_path} with modular specifications.")

if __name__ == "__main__":
    generate_spec()
