#!/usr/bin/env python3
"""Build the JLCPCB/PCBWay package: gerbers+drill zip, BOM and CPL in JLC format."""
import csv, os, subprocess, sys, zipfile, shutil
sys.path.insert(0, os.path.dirname(__file__))
from netlist import PARTS

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]
PCB = os.path.join(HERE, "visormesh.kicad_pcb")
G = os.path.join(OUT, "gerbers")
shutil.rmtree(G, ignore_errors=True); os.makedirs(G)

layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
subprocess.run(["kicad-cli", "pcb", "export", "gerbers", "-l", layers, "--subtract-soldermask",
                "--no-x2", "-o", G + "/", PCB], check=True, capture_output=True)
subprocess.run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
                "--excellon-zeros-format", "decimal", "--generate-map", "--map-format", "gerberx2",
                "-o", G + "/", PCB], check=True, capture_output=True)
zp = os.path.join(OUT, "visormesh_gerbers_jlcpcb.zip")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(G)):
        z.write(os.path.join(G, f), f)

# Placement
pos = os.path.join(OUT, "pos_raw.csv")
subprocess.run(["kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
                "--smd-only", "-o", pos, PCB], check=True, capture_output=True)
rows = list(csv.DictReader(open(pos)))
assembled = {r for r, p in PARTS.items() if p[3] and not p[5]}       # has LCSC and not DNP
with open(os.path.join(OUT, "visormesh_cpl_jlcpcb.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in rows:
        if r["Ref"] in assembled:
            w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm",
                        "Top" if r["Side"] == "top" else "Bottom", r["Rot"]])
os.remove(pos)

# BOM grouped by value+footprint+LCSC
groups = {}
for ref, (val, lib, name, lcsc, desc, dnp) in PARTS.items():
    if ref in assembled:
        groups.setdefault((val, name, lcsc), []).append(ref)
def refkey(r):
    import re
    m = re.match(r"([A-Z]+)(\d+)", r); return (m.group(1), int(m.group(2)))
with open(os.path.join(OUT, "visormesh_bom_jlcpcb.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
    for (val, name, lcsc), refs in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0])):
        w.writerow([val, ",".join(sorted(refs, key=refkey)), name, lcsc])
# Hand-fit parts
with open(os.path.join(OUT, "hand_fit_parts.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Ref", "Part", "Notes"])
    for ref, (val, lib, name, lcsc, desc, dnp) in PARTS.items():
        if ref not in assembled:
            w.writerow([ref, val, desc])
print("ok", sorted(os.listdir(G)))
