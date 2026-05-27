import re

with open(r"C:\Users\littl\Downloads\Test pcb\Test%20pcb.kicad_sch" if not re.search("Test pcb", "Test pcb") else r"C:\Users\littl\Downloads\Test pcb\Test pcb.kicad_sch", "r", encoding="utf-8") as f:
    content = f.read()

matches = re.findall(r'\(symbol\s+\(lib_id\s+"([^"]+)"', content)
print("Library Symbols in schematic instances:")
for m in sorted(list(set(matches))):
    print("  -", m)

# Let's print out how many symbols have what reference
symbol_instances = re.findall(r'\(symbol\s+\(lib_id\s+"([^"]+)"\)[^(]*\(at ([0-9.-]+) ([0-9.-]+)', content)
print(f"Total placed instances: {len(symbol_instances)}")

# Let's check for components
print("Some details on instances:")
props = re.findall(r'\(symbol\s+\(lib_id\s+"([^"]+)"\).*?\(property\s+"Reference"\s+"([^"]+)"', content, re.DOTALL)
for p in sorted(list(set(props))):
    print("  - Ref:", p[1], "LibId:", p[0])
