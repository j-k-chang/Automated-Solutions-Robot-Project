import re
import subprocess

filepath = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Let's try to remove lib_symbols block completely and see if it loads!
print("Testing with lib_symbols removed:")
content_no_lib = re.sub(r'\(lib_symbols.*?\n  \)', '', content, flags=re.DOTALL)
# Wait, if we just remove lib_symbols, let's see:
test_path = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\test_no_lib.kicad_sch"
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(content_no_lib)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc", test_path
], capture_output=True, text=True)
print("No lib_symbols - Return code:", res.returncode, "Stderr:", res.stderr.strip())

# Let's test with lib_symbols present but empty:
print("\nTesting with empty lib_symbols:")
content_empty_lib = re.sub(r'\(lib_symbols.*?\n  \)', '(lib_symbols)', content, flags=re.DOTALL)
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(content_empty_lib)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc", test_path
], capture_output=True, text=True)
print("Empty lib_symbols - Return code:", res.returncode, "Stderr:", res.stderr.strip())
