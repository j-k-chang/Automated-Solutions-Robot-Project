import subprocess

min_content = """(kicad_sch
	(version 20260306)
	(generator "eeschema")
	(generator_version "10.0")
	(uuid "6983a37b-f1f5-44eb-a441-3aa5d198ccbc")
	(paper "A4")
	(sheet_instances
		(path "/"
			(page "1")
		)
	)
	(embedded_fonts no)
)
"""

test_path = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\kicad-mcp-server\test_min_clean.kicad_sch"
with open(test_path, 'w', encoding='utf-8') as f:
    f.write(min_content)

res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc",
    test_path
], capture_output=True, text=True)

print("Return code:", res.returncode)
print("Stderr:", res.stderr.strip())
print("Stdout:", res.stdout.strip())
