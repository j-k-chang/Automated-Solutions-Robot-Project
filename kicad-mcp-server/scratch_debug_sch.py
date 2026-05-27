with open(r"C:\Users\littl\Downloads\Test pcb\Test pcb.kicad_sch", 'r', encoding='utf-8') as f:
    lines = f.readlines()

print("".join(lines[-100:]))
