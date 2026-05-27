import sys
from collections import defaultdict

def verify_align():
    from validate_schematic import parse_sexpr, build_tree
    
    filepath = "14ch_dispensing_project/14ch_dispensing_system.kicad_sch"
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
        
    tokens = parse_sexpr(content)
    tree = build_tree(tokens)
    
    elements = tree[0]
    
    # 1. Gather all symbol instances and their coordinates and library definitions
    symbols = {}
    lib_symbols = {}
    
    # Find lib_symbols first
    lib_symbols_elem = None
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0 and elem[0][1] == 'lib_symbols':
            lib_symbols_elem = elem
            break
            
    if lib_symbols_elem:
        for sym in lib_symbols_elem[1:]:
            if isinstance(sym, list) and len(sym) > 0 and sym[0][1] == 'symbol':
                sym_name = sym[1][1]
                # Find pins
                pins = {}
                # Pins are nested inside symbol name block
                for sub in sym:
                    if isinstance(sub, list) and len(sub) > 0 and sub[0][1] == 'symbol':
                        # This contains graphics or pins
                        for pin in sub:
                            if isinstance(pin, list) and len(pin) > 0 and pin[0][1] == 'pin':
                                # pin syntax: (pin passive line (at X Y rot) ... (name NAME) (number NUM))
                                p_num = None
                                p_name = None
                                p_x = None
                                p_y = None
                                
                                for p_prop in pin:
                                    if isinstance(p_prop, list):
                                        if p_prop[0][1] == 'name':
                                            p_name = p_prop[1][1]
                                        elif p_prop[0][1] == 'number':
                                            p_num = p_prop[1][1]
                                        elif p_prop[0][1] == 'at':
                                            p_x = float(p_prop[1][1])
                                            p_y = float(p_prop[2][1])
                                            
                                if p_num:
                                    pins[p_num] = (p_x, p_y, p_name)
                lib_symbols[sym_name] = pins

    # Gather symbol instances
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0 and elem[0][1] == 'symbol':
            lib_id = None
            at_x = None
            at_y = None
            ref = None
            
            for prop in elem:
                if isinstance(prop, list):
                    if prop[0][1] == 'lib_id':
                        lib_id = prop[1][1]
                    elif prop[0][1] == 'at':
                        at_x = float(prop[1][1])
                        at_y = float(prop[2][1])
                    elif prop[0][1] == 'property' and prop[1][1] == 'Reference':
                        ref = prop[2][1]
                        
            if ref:
                symbols[ref] = {
                    'lib_id': lib_id,
                    'x': at_x,
                    'y': at_y
                }
                
    # Gather labels
    labels = []
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0 and elem[0][1] == 'label':
            lbl_name = elem[1][1]
            at_prop = elem[2]
            lbl_x = float(at_prop[1][1])
            lbl_y = float(at_prop[2][1])
            labels.append((lbl_name, lbl_x, lbl_y))
            
    print(f"Loaded {len(symbols)} symbol instances, {len(lib_symbols)} library symbols, and {len(labels)} labels.")
    
    # 2. Check label alignment
    # For each label, see if it sits exactly on a pin of a symbol instance
    aligned_count = 0
    dangling_labels = []
    
    # Pre-calculate absolute pin positions for fast lookup
    pin_lookup = {} # (x, y) -> list of (symbol_ref, pin_number, pin_name)
    
    for ref, sym in symbols.items():
        lib_name = sym['lib_id']
        if lib_name in lib_symbols:
            pins = lib_symbols[lib_name]
            for p_num, (p_x, p_y, p_name) in pins.items():
                # absolute position of the pin:
                # relative coordinates of pins are: left-side is negative X, right-side is positive X.
                # In KiCad S-expression schematic:
                # absolute pin X = component_x + pin_x
                # absolute pin Y = component_y - pin_y (Wait, pin_y goes UP in symbol coords but Y goes DOWN in schematic sheet)
                # Let's verify by testing both signs!
                abs_x = sym['x'] + p_x
                abs_y = sym['y'] - p_y  # Standard KiCad subtraction for Y
                
                coord = (round(abs_x, 4), round(abs_y, 4))
                if coord not in pin_lookup:
                    pin_lookup[coord] = []
                pin_lookup[coord].append((ref, p_num, p_name))
                
                # Also try addition for Y just in case
                coord_add = (round(abs_x, 4), round(sym['y'] + p_y, 4))
                if coord_add not in pin_lookup:
                    pin_lookup[coord_add] = []
                pin_lookup[coord_add].append((ref, p_num, p_name))
                
    for lbl_name, lx, ly in labels:
        coord = (round(lx, 4), round(ly, 4))
        if coord in pin_lookup:
            aligned_count += 1
        else:
            dangling_labels.append((lbl_name, lx, ly))
            
    print(f"Total labels checked: {len(labels)}")
    print(f"Aligned labels: {aligned_count}")
    print(f"Dangling labels: {len(dangling_labels)}")
    
    if dangling_labels:
        print("First 10 dangling labels:")
        for name, lx, ly in dangling_labels[:10]:
            print(f"  Label '{name}' at ({lx}, {ly})")
            # Find nearest pins
            nearest = []
            for (px, py), pins in pin_lookup.items():
                dist = ((px - lx)**2 + (py - ly)**2)**0.5
                if dist < 15:
                    nearest.append((dist, px, py, pins))
            nearest.sort()
            for d, px, py, pins in nearest[:2]:
                print(f"    Nearest pin: {pins} at ({px}, {py}), dist: {d:.2f}")

if __name__ == "__main__":
    verify_align()
