import glob
import os
import shutil
import zlib


for directory in ("ui", "languages", "licenses"):
    os.makedirs(f"build/nitrofs/{directory}", exist_ok=True)
shutil.copyfile("out/nds-bootstrap.nds", "build/nitrofs/bootstrap.nds")
shutil.copyfile("assets/startup.bin", "build/nitrofs/ui/startup.bin")
shutil.copyfile("build/rom-save-types.bin", "build/nitrofs/rom-save-types.bin")
for path in sorted(glob.glob("texts/*.json")):
    shutil.copyfile(path, f"build/nitrofs/languages/{path[6:]}")
for path, name in (
    ("LICENSE", "NitroPatcherDS.txt"),
    ("vendor/md4c/LICENSE.md", "md4c.txt"),
    ("vendor/fmt/LICENSE", "fmt.txt"),
    ("vendor/BlzHelper/LICENSE", "BlzHelper.txt"),
    ("assets/fonts/Gulim-OFL.txt", "Gulim-OFL.txt"),
    ("assets/fonts/Gulim-AUTHORS.txt", "Gulim-AUTHORS.txt"),
    ("/opt/np3d/liblzma/COPYING", "liblzma.txt"),
):
    shutil.copyfile(path, f"build/nitrofs/licenses/{name}")
with open("out/nds-bootstrap.nds", "rb") as reader:
    bootstrap = reader.read()
with open("build/nitrofs/bootstrap.nds.version", "w", -1, "utf8", None, "\n") as writer:
    writer.write(f"{zlib.crc32(bootstrap)}:{len(bootstrap)}")
