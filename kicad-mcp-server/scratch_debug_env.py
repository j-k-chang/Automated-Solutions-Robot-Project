import shutil
import subprocess

src = r"C:\Users\littl\Downloads\Test pcb\Test pcb.kicad_sch"
dst = r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\test_copy.kicad_sch"

shutil.copy(src, dst)
print("Copied Test pcb.kicad_sch to project folder.")

print("Running ERC on copied file:")
res = subprocess.run([
    r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    "sch", "erc", dst
], capture_output=True, text=True)

print("Return code:", res.returncode)
print("Stdout:", res.stdout[:500])
print("Stderr:", res.stderr[:500])
