import re
import subprocess

filepath = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Replace Connector_Generic with Connector
content_new = content.replace("Connector_Generic:", "Connector:")

# Also let's try changing version to 20260306 to match KiCad 10.0
content_new = re.sub(r'\(version \d+\)', '(version 20260306)', content_new)

test_path = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\test_load.kicad_sch"
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(content_new)

print("Running test load ERC check...")
res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc",
    test_path
], capture_output=True, text=True)

print("Return code:", res.returncode)
print("Stdout:", res.stdout[:500])
print("Stderr:", res.stderr[:500])
