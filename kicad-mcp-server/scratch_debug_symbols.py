import re
import subprocess

filepath = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Let's remove all symbol instances from the sheet
print("Testing with all symbol instances removed:")
content_no_symbols = re.sub(r'  \(symbol \(lib_id.*?\n  \)\n', '', content, flags=re.DOTALL)

test_path = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\test_no_symbols.kicad_sch"
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(content_no_symbols)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc", test_path
], capture_output=True, text=True)
print("No symbol instances - Return code:", res.returncode, "Stderr:", res.stderr.strip())

# Let's remove all label instances from the sheet
print("\nTesting with all label instances removed:")
content_no_labels = re.sub(r'  \(label.*?\n  \)\n', '', content, flags=re.DOTALL)
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(content_no_labels)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc", test_path
], capture_output=True, text=True)
print("No label instances - Return code:", res.returncode, "Stderr:", res.stderr.strip())
