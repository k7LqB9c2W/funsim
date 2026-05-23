from pathlib import Path

script = r'''from PIL import Image
from pathlib import Path

TARGET_SIZE = (80, 64)

# Exact filenames from your assets folder screenshot.
# Only these files will be edited. Nothing else.
FILES = [
    "workshop.png",
    "bank.png",
    "university.png",
    "factory.png",
    "powerplant.png",
    "airstrip.png",
    "reactor.png",
    "satellite.png",
    "robot_lab.png",
]

for filename in FILES:
    path = Path(filename)

    if not path.is_file():
        print(f"Missing, skipped: {filename}")
        continue

    img = Image.open(path).convert("RGBA")
    resized = img.resize(TARGET_SIZE, Image.Resampling.LANCZOS)
    resized.save(path)

    print(f"Resized: {filename} -> 80x64")
'''

out = Path("/mnt/data/resize_exact_sprites.py")
out.write_text(script)
print(f"Saved: {out}")
