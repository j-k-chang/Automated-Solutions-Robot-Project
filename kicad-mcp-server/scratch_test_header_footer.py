import re
import subprocess

filepath = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Let's extract the lib_symbols and all subsequent parts of the schematic.
# We'll locate the first occurrence of (lib_symbols or (symbol
m = re.search(r'\((lib_symbols|symbol|label)', content)
if not m:
    print("Could not find start of elements!")
    exit(1)

start_elements = m.start()

# Now find where the footer starts.
# Our generated schematic ends with:
#   (root_sheet_instance (path "/") (page "1"))
# )
# Let's locate the last root_sheet_instance.
m_end = re.search(r'\s*\(root_sheet_instance.*?\)\s*\)\s*$', content, re.DOTALL)
if not m_end:
    print("Could not find end footer!")
    exit(1)

end_elements = m_end.start()

elements_content = content[start_elements:end_elements]

# Construct a new schematic with working header and footer
new_content = f"""(kicad_sch
	(version 20260306)
	(generator "eeschema")
	(generator_version "10.0")
	(uuid "6983a37b-f1f5-44eb-a441-3aa5d198ccbc")
	(paper "A3")
{elements_content}
	(sheet_instances
		(path "/"
			(page "1")
		)
	)
	(embedded_fonts no)
)
"""

test_path = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\kicad-mcp-server\test_converted.kicad_sch"
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(new_content)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc",
    test_path
], capture_output=True, text=True)

print("Return code:", res.returncode)
print("Stderr:", res.stderr.strip())
print("Stdout:", res.stdout[:500])
