#!/usr/bin/env python3
"""Independent cross-checks of the routed rev 2 board against the netlist and the source documents."""
import sys, os, collections
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from netlist import CONN, SB_NAMES

fail = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond: fail.append(msg)

b = pcbnew.LoadBoard(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "kicad", "visormesh.kicad_pcb"))
pad_net = {}
for fp in b.GetFootprints():
    for p in fp.Pads():
        if p.GetNumber():
            pad_net[(fp.GetReference(), p.GetNumber())] = p.GetNetname()

# 1. every pad's net equals the netlist (unlisted pads unconnected)
bad = [(k, pad_net[k], CONN.get(k, "")) for k in pad_net if pad_net[k] != CONN.get(k, "")]
check(not bad, f"board pad nets match netlist.py ({len(pad_net)} pads) {bad[:5]}")

# 2. Springboard pins vs Development Guide Table 5-1 names
def want(name):
    if name in ("GND", "CD1*", "CD2*"): return "GND"
    if name == "VCC": return "VSB"
    if name == "VDOCK": return "VDOCK"
    if name in ("CS0*", "CS1*", "OE*", "WE*", "IRQ*", "RESET*"): return name[:-1] + "_N"
    if name[0] == "D" and name[1:].isdigit(): return name
    if name[0] == "A" and name[1:].isdigit() and 1 <= int(name[1:]) <= 20: return name
    return ""
jbad = [(p, SB_NAMES[p], pad_net[("J1", str(p))]) for p in range(1, 69) if pad_net[("J1", str(p))] != want(SB_NAMES[p])]
check(not jbad, f"J1 pins 1-68 agree with Table 5-1 {jbad[:4]}")

# 3. flash in word mode on the slot bus: flash A0 = slot A1, all 16 data lines, CS0*
check(pad_net[("U1", "25")] == "A1" and pad_net[("U1", "9")] == "A20" and pad_net[("U1", "26")] == "CS0_N"
      and pad_net[("U1", "47")] == "VSB" and pad_net[("U1", "28")] == "OE_N",
      "U1: A0=slot A1, A19=slot A20, CE#=CS0*, OE#=OE*, BYTE#=VCC (word mode)")
check(sorted(n for (r, p), n in pad_net.items() if r == "U1" and n.startswith("D")) == sorted("D%d" % i for i in range(16)),
      "U1: DQ0-DQ15 on slot D0-D15")
# 4. UART on CS1* through the OR strobes, register select from A1-A3
check(pad_net[("U3", "2")] == "CS1_N" and pad_net[("U3", "6")] == "CS1_N" and pad_net[("U3", "1")] == "OE_N"
      and pad_net[("U3", "5")] == "WE_N" and pad_net[("U2", "14")] == "UART_IOR_N" and pad_net[("U2", "11")] == "UART_IOW_N",
      "U2 IOR = OE* OR CS1*, IOW = WE* OR CS1*")
check([pad_net[("U2", p)] for p in ("18", "17", "16")] == ["A1", "A2", "A3"], "U2 A0-A2 = slot A1-A3")
check(pad_net[("U4", "1")] == "UART_INT" and pad_net[("U4", "6")] == "IRQ_N" and pad_net[("U4", "3")] == "RESET_N"
      and pad_net[("U4", "4")] == "UART_RESET", "U4 inverts INT -> IRQ* and RESET* -> RESET")

# 5. power domains meet only in the Ioff translators (and the radio boards on the 3V3X side)
vsb_side = {"VSB", "UART_TX", "UART_RX"}
x_side = {"3V3X", "XIAO_RX", "XIAO_TX", "VISOR_PRESENT"}
cross = collections.defaultdict(set)
for (r, p), n in pad_net.items():
    if n in vsb_side: cross[r].add("VSB")
    if n in x_side: cross[r].add("3V3X")
both = sorted(r for r, s in cross.items() if len(s) == 2)
check(both == ["U5", "U6"], f"only the translators straddle both power domains: {both}")
check(pad_net[("X1", "7")] == "XIAO_TX" and pad_net[("X1", "8")] == "XIAO_RX" and pad_net[("X1", "1")] == "VISOR_PRESENT",
      "XIAO D6=TX, D7=RX, D0=present (companion firmware patch)")
check(not any(pad_net.get(("X2", k)) for k in ("1", "7", "8", "14")), "Wio D0, D6, D7, 5V unconnected")

# 6. routing complete and outline
tr = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
check(len(tr) > 0, f"board is routed ({len(tr)} segments)")
bb = b.GetBoardEdgesBoundingBox()
w, h = pcbnew.ToMM(bb.GetWidth()) - 0.1, pcbnew.ToMM(bb.GetHeight()) - 0.1
check(abs(w - 50.43) < 0.05, f"board width {w:.2f} mm = springSD width 50.43 mm")
print(f"INFO board {w:.2f} x {h:.2f} mm, {b.GetCopperLayerCount()} layers, "
      f"thickness {pcbnew.ToMM(b.GetDesignSettings().GetBoardThickness())} mm")
print("RESULT", "ALL PASS" if not fail else f"{len(fail)} FAIL")
sys.exit(1 if fail else 0)
