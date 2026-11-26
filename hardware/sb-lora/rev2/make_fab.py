#!/usr/bin/env python3
"""Build rev 2's fab packages: gerbers+drill zip (4 layers), and a BOM and CPL each for JLCPCB (LCSC numbers)
and PCBWay (manufacturer part numbers, hand-fit parts listed as do-not-place). Also the hand-fit list and a 1:1
top copper print for checking the XIAO and Wio footprints against the real boards, and an assembly drawing
(assembly_drawing.py, run with KiCad's Python) marking pin 1 of every part with an orientation.
Usage: make_fab.py <out dir> [kicad-cli]"""
import csv, glob, os, subprocess, sys, zipfile, shutil
sys.path.insert(0, os.path.dirname(__file__))
from netlist import PARTS, MFR

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]
PCB = os.path.join(HERE, "kicad", "visormesh.kicad_pcb")
CLI = sys.argv[2] if len(sys.argv) > 2 else "/opt/homebrew/Caskroom/kicad/10.0.8/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
G = os.path.join(OUT, "gerbers")
shutil.rmtree(G, ignore_errors=True); os.makedirs(G)

layers = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
subprocess.run([CLI, "pcb", "export", "gerbers", "-l", layers, "--subtract-soldermask",
                "--no-x2", "-o", G + "/", PCB], check=True, capture_output=True)
subprocess.run([CLI, "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
                "--excellon-zeros-format", "decimal", "--generate-map", "--map-format", "gerberx2",
                "-o", G + "/", PCB], check=True, capture_output=True)
zp = os.path.join(OUT, "visormesh_gerbers_jlcpcb.zip")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(G)):
        z.write(os.path.join(G, f), f)
shutil.copyfile(zp, os.path.join(OUT, "visormesh_gerbers_pcbway.zip"))

# Placement
pos = os.path.join(OUT, "pos_raw.csv")
subprocess.run([CLI, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
                "--smd-only", "-o", pos, PCB], check=True, capture_output=True)
rows = list(csv.DictReader(open(pos)))
assembled = {r for r, p in PARTS.items() if p[3] and not p[5]}       # has LCSC and not DNP
missing = assembled - {r["Ref"] for r in rows}
assert not missing, "not in the placement export: %s" % sorted(missing)
for name in ("visormesh_cpl_jlcpcb.csv", "visormesh_cpl_pcbway.csv"):    # PCBWay reads JLC's layout as is
    with open(os.path.join(OUT, name), "w", newline="") as f:
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
# PCBWay BOM: they buy from distributors, so manufacturer part numbers; LCSC kept as a cross-reference
nomfr = {lcsc for (_, _, lcsc) in groups} - set(MFR)
assert not nomfr, "no manufacturer part number in netlist.MFR for: %s" % sorted(nomfr)
with open(os.path.join(OUT, "visormesh_bom_pcbway.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Item", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description", "Package", "Type", "LCSC Part #", "Notes"])
    n = 0
    for (val, name, lcsc), refs in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0])):
        mfr, mpn, spec = MFR[lcsc]; n += 1
        w.writerow([n, ",".join(sorted(refs, key=refkey)), len(refs), mfr, mpn, spec, name, "SMD", lcsc, ""])
    for ref, (val, lib, name, lcsc, desc, dnp) in PARTS.items():
        if ref not in assembled and not ref.startswith(("JP", "TP")):   # jumpers and test points are bare copper
            n += 1
            w.writerow([n, ref, 1, "", val, desc, name, "DNP", "", "Do not place: fitted by the customer"])
# Hand-fit parts
with open(os.path.join(OUT, "hand_fit_parts.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Ref", "Part", "Notes"])
    for ref, (val, lib, name, lcsc, desc, dnp) in PARTS.items():
        if ref not in assembled:
            w.writerow([ref, val, desc])
# 1:1 top copper and outline, to lay the XIAO and Wio on before ordering
subprocess.run([CLI, "pcb", "export", "pdf", "--layers", "F.Cu,Edge.Cuts", "--mode-single", "--black-and-white",
                "--scale", "1", "-o", os.path.join(OUT, "check_top_copper_1to1.pdf"), PCB], check=True, capture_output=True)
# Assembly drawing: needs pcbnew, so KiCad's own Python
KPY = sorted(glob.glob(os.path.join(os.path.dirname(os.path.dirname(CLI)),
                                    "Frameworks/Python.framework/Versions/3*/bin/python3")))[-1]
subprocess.run([KPY, os.path.join(HERE, "assembly_drawing.py"), PCB, os.path.join(OUT, "assembly_top.pdf"), CLI],
               check=True, capture_output=True)
print("ok", sorted(os.listdir(G)))
