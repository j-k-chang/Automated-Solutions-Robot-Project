import re

filepath = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

print("Original content length:", len(content))

# Count symbols
symbols_cnt = len(re.findall(r'\(symbol', content))
print("Number of '(symbol' tokens:", symbols_cnt)

# Count properties
prop_cnt = len(re.findall(r'\(property', content))
print("Number of '(property' tokens:", prop_cnt)
