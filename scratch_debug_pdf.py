import pypdf
# Wait, let's see if pypdf is installed or use standard libraries.
# If pypdf is not installed, let's see if we can extract text using a python script.
import subprocess
import sys

try:
    import pypdf
except ImportError:
    subprocess.run([sys.executable, "-m", "pip", "install", "pypdf"], check=True)
    import pypdf

reader = pypdf.PdfReader(r"C:\Users\littl\Downloads\TMC2209-V1.2.pdf")
print("Total pages:", len(reader.pages))

found = False
for page_num, page in enumerate(reader.pages):
    text = page.extract_text()
    if "MS1" in text and "MS2" in text and "standalone" in text.lower():
        print(f"--- Found on Page {page_num + 1} ---")
        # Print lines containing MS1 and MS2
        for line in text.split('\n'):
            if any(k in line for k in ["MS1", "MS2", "standalone", "microstep"]):
                print(line)
        found = True

if not found:
    print("Not found with exact keyword combination, searching page-by-page...")
    # Just print the table if we find MS1
    for page_num, page in enumerate(reader.pages[:20]):
        text = page.extract_text()
        if "MS1" in text:
            print(f"--- MS1 found on Page {page_num + 1} ---")
            for line in text.split('\n')[:20]:
                print(line)
