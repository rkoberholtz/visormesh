#!/usr/bin/env python3
"""Assembly drawing for the assembler: the board's fab and silkscreen layers, a ring round pin 1 (D1's
cathode) of every part that has an orientation, and a table saying where pin 1 is. Run with KiCad's Python
(make_fab.py does). Usage: assembly_drawing.py <board.kicad_pcb> <out.pdf> <kicad-cli>"""
import os, subprocess, sys, tempfile
import pcbnew

PCB, PDF, CLI = sys.argv[1:4]
POLAR = ["U1", "U2", "U3", "U4", "U5", "U6", "Y1", "D1"]     # the passives go either way round
MM = pcbnew.FromMM

b = pcbnew.LoadBoard(PCB)
L = pcbnew.Dwgs_User
box = b.GetBoardEdgesBoundingBox()
X_TEXT, Y_TEXT = pcbnew.ToMM(box.GetRight()) + 8, pcbnew.ToMM(box.GetTop())

def text(s, x, y, size=1.4):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s); t.SetLayer(L); t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size))); t.SetTextThickness(MM(size * 0.15))
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    b.Add(t)

def where(dx, dy):
    # Drawing directions, with the J1 (connector) edge at the bottom of the page
    v = "top" if dy < -0.2 else "bottom" if dy > 0.2 else ""
    h = "left" if dx < -0.2 else "right" if dx > 0.2 else ""
    return " ".join(w for w in (v, h) if w) or "centre"

rows = []
for ref in POLAR:
    fp = b.FindFootprintByReference(ref)
    pad = fp.FindPadByNumber("1")
    c, p = fp.GetPosition(), pad.GetPosition()
    ring = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_CIRCLE)
    ring.SetLayer(L); ring.SetWidth(MM(0.2)); ring.SetCenter(p)
    ring.SetEnd(pcbnew.VECTOR2I(p.x + MM(1.0), p.y))
    b.Add(ring)
    side = where(pcbnew.ToMM(p.x - c.x), pcbnew.ToMM(p.y - c.y))
    what = "cathode (band)" if ref == "D1" else "pin 1"
    rows.append((ref, fp.GetValue(), "%s %s" % (what, side)))

text("SB-LoRa rev 2: assembly, top side", X_TEXT, Y_TEXT, 2.0)
text("Rings mark pin 1 (D1: cathode).", X_TEXT, Y_TEXT + 4)
text("J1 (connector) edge is at the bottom.", X_TEXT, Y_TEXT + 6.5)
for i, r in enumerate(rows):
    for col, x in zip(r, (0, 6, 34)):          # the font is proportional, so each column is its own text
        text(col, X_TEXT + x, Y_TEXT + 11 + i * 2.5, 1.3)
y = Y_TEXT + 11 + len(rows) * 2.5 + 3
for i, s in enumerate(["Passives (C, R) are not polarised.",
                       "Do not place: J1, X1 (XIAO), X2 (Wio), BT1, TP1-TP4.",
                       "JP2 is bridged in copper; JP1 stays open."]):
    text(s, X_TEXT, y + i * 2.5, 1.3)

with tempfile.TemporaryDirectory() as tmp:
    tpcb = os.path.join(tmp, "assembly.kicad_pcb")
    b.Save(tpcb)
    subprocess.run([CLI, "pcb", "export", "pdf", "--layers", "F.Fab,F.SilkS,Edge.Cuts,User.Drawings",
                    "--mode-single", "--black-and-white", "-o", PDF, tpcb], check=True, capture_output=True)
for r in rows:
    print("  ".join(r))
