#!/usr/bin/env python3
"""Write the rev 2 custom footprints into visormesh.pretty (KiCad 7 format).

Positions come from Seeed's own KiCad files (github.com/Seeed-Studio/OPL_Kicad_Library):
  XIAO-nRF52840-SMD.kicad_mod and "Seeed Studio XIAO nRF52840 v1.3.kicad_pcb" for the XIAO,
  "Wio-SX1262 for XIAO nRF52840.kicad_pcb" for the Wio.
All three footprints share one frame: top view, origin at the centre of the 14-pin pattern,
pin 1 (D0) top left, USB-C toward -y, pins 1-7 down the left (x = -7.62), 8-14 up the right.
That is how the Wio sits under the XIAO when stacked, so pin k means the same signal on both.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "visormesh.pretty")
ROW = 7.62           # header rows 15.24 mm apart, 1.27 mm in from each board edge
PITCH = 2.54

def pin_xy(k):
    if k <= 7:
        return -ROW, -ROW + PITCH * (k - 1)
    return ROW, ROW - PITCH * (k - 8)

def rect(x0, y0, x1, y1, layer, w):
    return ('  (fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width %.2f) (type solid)) (fill none) (layer "%s"))'
            % (x0, y0, x1, y1, w, layer))

def line(x0, y0, x1, y1, layer, w):
    return ('  (fp_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width %.2f) (type solid)) (layer "%s"))'
            % (x0, y0, x1, y1, w, layer))

def text(kind, s, x, y, layer, size=0.8, hide=False):
    h = " hide" if hide else ""
    return ('  (fp_text %s "%s" (at %.3f %.3f) (layer "%s")%s (effects (font (size %.2f %.2f) (thickness %.2f))))'
            % (kind, s, x, y, layer, h, size, size, size * 0.15))

def write(name, descr, body, attr="through_hole"):
    with open(os.path.join(OUT, name + ".kicad_mod"), "w") as f:
        f.write('(footprint "%s" (version 20221018) (generator make_footprints)\n' % name)
        f.write('  (layer "F.Cu")\n  (descr "%s")\n  (attr %s exclude_from_bom)\n' % (descr, attr))
        f.write("\n".join(body) + "\n)\n")

def xiao():
    # Board 17.78 x 20.95 mm (Seeed courtyard y -21.05..-0.1 in their frame = -10.55..+10.40 here).
    W2, TOP, BOT = 8.89, -10.55, 10.40
    b = [text("reference", "REF**", 0, TOP - 1.2, "F.SilkS"), text("value", "XIAO_SMD", 0, BOT + 1.2, "F.Fab"),
         text("user", "USB", 0, TOP + 1.2, "F.SilkS")]
    # Edge pads: Seeed's 2.75 x 2 mm castellation pads (centres 8.08 mm out, past the board edge for
    # the fillet), each with a plated hole under the XIAO's own header hole so the pin header can
    # also be pushed through and cut flush.
    for k in range(1, 15):
        x, y = pin_xy(k)
        cx = x - 0.46 if x < 0 else x + 0.46
        off = x - cx
        b.append('  (pad "%d" thru_hole roundrect (at %.3f %.3f) (size 2.75 2) (drill 1 (offset %.2f 0)) '
                 '(layers "*.Cu" "*.Mask") (roundrect_rratio 0.25))' % (k, cx, y, off))
    # Bottom pads, from the XIAO v1.3 board file (BAT0, GND0, TP1-TP3, TP5). Each is a plated hole
    # filled with solder from the carrier's back so it wicks up to the XIAO's pad. NFC1/NFC2
    # (x 3.81/5.715, y 9.24) get nothing: the carrier's mask covers its copper there.
    for pad, x, y, shape in (("BAT+", -4.445, -0.314, "oval"), ("BAT-", -4.445, -2.219, "oval"),
                             ("SWDIO", -1.27, -8.569, "circle"), ("SWCLK", 1.27, -8.569, "circle"),
                             ("RST", -1.27, -6.029, "circle"), ("GND", 1.27, -6.029, "circle")):
        size = "2.2 1.3" if shape == "oval" else "1.5 1.5"
        b.append('  (pad "%s" thru_hole %s (at %.3f %.3f) (size %s) (drill 0.8) (layers "*.Cu" "*.Mask"))'
                 % (pad, shape, x, y, size))
    b.append(text("user", "BAT+", -4.445 - 2.4, -0.314, "B.SilkS", 0.6))
    b.append(text("user", "BAT-", -4.445 - 2.4, -2.219, "B.SilkS", 0.6))
    b.append(rect(-W2, TOP, W2, BOT, "F.Fab", 0.1))
    b.append(rect(-W2 - 0.25, TOP - 0.25, W2 + 0.25, BOT + 0.25, "F.CrtYd", 0.05))
    # Silk outline between the pad columns only, plus a pin-1 mark.
    for xx in (-6.4, 6.4):
        b.append(line(xx, TOP, xx, BOT, "F.SilkS", 0.12))
    b.append(line(-6.4, TOP, 6.4, TOP, "F.SilkS", 0.12))
    b.append(line(-6.4, BOT, 6.4, BOT, "F.SilkS", 0.12))
    b.append('  (fp_circle (center -10.2 -7.62) (end -9.95 -7.62) (stroke (width 0.25) (type solid)) (fill solid) (layer "F.SilkS"))')
    write("XIAO_SMD", "Seeed XIAO nRF52840, soldered flat by its castellated pads or by its pin header; "
          "bottom BAT/SWD pads reached through solder-filled plated holes", b)

def wio():
    # Board 17.78 x 21.44 mm: top edge 2.921 mm above pin 1's row, bottom edge 3.279 mm below pin 7's.
    W2, TOP, BOT = 8.89, -ROW - 2.921, ROW + 3.279
    b = [text("reference", "REF**", 0, TOP - 1.2, "F.SilkS"), text("value", "WIO_PTH", 0, BOT + 1.2, "F.Fab"),
         text("user", "WIO", 0, 0, "F.Fab")]
    for k in range(1, 15):
        x, y = pin_xy(k)
        shape = "rect" if k == 1 else "circle"
        b.append('  (pad "%d" thru_hole %s (at %.3f %.3f) (size 1.651 1.651) (drill 1) (layers "*.Cu" "*.Mask"))'
                 % (k, shape, x, y))
    b.append(rect(-W2, TOP, W2, BOT, "F.Fab", 0.1))
    b.append(rect(-W2, TOP, W2, BOT, "F.SilkS", 0.12))
    b.append(rect(-W2 - 0.25, TOP - 0.25, W2 + 0.25, BOT + 0.25, "F.CrtYd", 0.05))
    write("WIO_PTH", "Seeed Wio-SX1262 for XIAO: male pins through the carrier, cut flush", b)

def bat():
    b = [text("reference", "REF**", 0, -2.8, "F.SilkS"), text("value", "BAT_PADS", 0, 2.8, "F.Fab"),
         text("user", "+", -2.0, -2.8, "F.SilkS"), text("user", "-", 2.0, -2.8, "F.SilkS")]
    for pad, x in (("1", -2.0), ("2", 2.0)):
        b.append('  (pad "%s" smd roundrect (at %.3f 0) (size 2.5 3.5) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.2))'
                 % (pad, x))
    b.append(rect(-3.5, -2.0, 3.5, 2.0, "F.CrtYd", 0.05))
    write("BAT_PADS", "LiPo lead solder pads: 1 = +, 2 = -", b, attr="smd")

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    xiao(); wio(); bat()
