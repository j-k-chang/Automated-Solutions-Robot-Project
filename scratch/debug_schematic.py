import subprocess
import os

def test_schematic(content):
    temp_path = os.path.abspath("14ch_dispensing_project/temp_debug.kicad_sch")
    temp_pro_path = os.path.abspath("14ch_dispensing_project/temp_debug.kicad_pro")
    
    with open(temp_path, "w", encoding="utf-8") as f:
        f.write(content)
    
    with open(temp_pro_path, "w", encoding="utf-8") as f:
        f.write('{"version": 1, "uuid": "abeba167-1d21-43ce-8d70-4f1c0f2e2fdd", "title": "temp"}')
        
    try:
        # Run kicad-cli sch erc
        res = subprocess.run(
            ['C:\\Program Files\\KiCad\\10.0\\bin\\kicad-cli.exe', 'sch', 'erc', temp_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        success = "Failed to load schematic" not in res.stderr and "Failed to load schematic" not in res.stdout and res.returncode == 0
        return success, res.stdout, res.stderr
    finally:
        if os.path.exists(temp_path):
            os.remove(temp_path)
        if os.path.exists(temp_pro_path):
            os.remove(temp_pro_path)

def main():
    filepath = "14ch_dispensing_project/14ch_dispensing_system.kicad_sch"
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        
    print(f"Total lines in original schematic: {len(lines)}")
    
    # Test 1: Just a minimal schematic
    minimal = '(kicad_schematic (version 20240108) (generator "kicad-pipeline") (generator_version "1.0") (uuid "abeba167-1d21-43ce-8d70-4f1c0f2e2fdd") (paper "A3") (title_block (title "Test")) (root_sheet_instance (path "/") (page "1")))\n'
    ok, out, err = test_schematic(minimal)
    print(f"Minimal schematic test: {ok} (err: {err.strip()})")
    
    # Test 2: Schematic with title block and root_sheet_instance but no symbols/labels
    test2 = [
        "(kicad_schematic",
        "  (version 20240108)",
        '  (generator "kicad-pipeline")',
        '  (generator_version "1.0")',
        '  (uuid "abeba167-1d21-43ce-8d70-4f1c0f2e2fdd")',
        '  (paper "A3")',
        "  (title_block",
        '    (title "Test")',
        "  )",
        '  (root_sheet_instance (path "/") (page "1"))',
        ")"
    ]
    ok, out, err = test_schematic("\n".join(test2))
    print(f"Test 2 (Title Block only): {ok} (err: {err.strip()})")

    # Let's perform binary search or division
    # We will test:
    # 1. lib_symbols block only
    # 2. symbols instances only
    # 3. labels only
    
    # Let's parse the top-level tree elements
    from validate_schematic import parse_sexpr, build_tree
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    tokens = parse_sexpr(content)
    tree = build_tree(tokens)
    
    elements = tree[0] # The list inside kicad_schematic
    print(f"Number of top-level elements: {len(elements)}")
    
    # We will reconstruct the schematic with subsets of elements
    def reconstruct_and_test(sub_elements):
        # elements are representation of S-expression lists
        # let's format them back to S-expression string
        def format_node(node):
            if isinstance(node, list):
                return "(" + " ".join(format_node(n) for n in node) + ")"
            elif node[0] == 'STRING':
                # Escape quotes and wrap in quotes
                escaped = node[1].replace('"', '\\"')
                return f'"{escaped}"'
            else:
                return str(node[1])
                
        sexpr = "(kicad_schematic\n"
        for elem in sub_elements:
            sexpr += "  " + format_node(elem) + "\n"
        sexpr += ")"
        return test_schematic(sexpr)
        
    # Let's test with just paper, title_block, and root_sheet_instance from the original
    essentials = []
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0:
            name = elem[0][1]
            if name in ('version', 'generator', 'generator_version', 'uuid', 'paper', 'title_block', 'root_sheet_instance'):
                essentials.append(elem)
                
    ok, out, err = reconstruct_and_test(essentials)
    print(f"Essentials only: {ok} (err: {err.strip()})")
    
    # Let's add lib_symbols
    lib_symbols_elem = None
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0 and elem[0][1] == 'lib_symbols':
            lib_symbols_elem = elem
            break
            
    if lib_symbols_elem:
        ok, out, err = reconstruct_and_test(essentials + [lib_symbols_elem])
        print(f"Essentials + lib_symbols: {ok} (err: {err.strip()})")
        
    # Let's add symbols (symbol instances) one by one or all
    symbol_elems = []
    label_elems = []
    for elem in elements:
        if isinstance(elem, list) and len(elem) > 0:
            name = elem[0][1]
            if name == 'symbol':
                symbol_elems.append(elem)
            elif name == 'label':
                label_elems.append(elem)
                
    print(f"Found {len(symbol_elems)} symbol instances and {len(label_elems)} label instances")
    
    if symbol_elems:
        ok, out, err = reconstruct_and_test(essentials + [lib_symbols_elem] + symbol_elems)
        print(f"Essentials + lib_symbols + all symbols: {ok} (err: {err.strip()})")
        if not ok:
            # Let's find which symbol causes the error
            for i, sym in enumerate(symbol_elems):
                ok, out, err = reconstruct_and_test(essentials + [lib_symbols_elem] + [sym])
                if not ok:
                    print(f"  Symbol {i} causes error: {sym[1][1] if len(sym) > 1 else 'unknown'} (err: {err.strip()})")
                    break
                    
    if label_elems:
        ok, out, err = reconstruct_and_test(essentials + [lib_symbols_elem] + symbol_elems + label_elems)
        print(f"Full schematic: {ok} (err: {err.strip()})")

if __name__ == "__main__":
    main()
