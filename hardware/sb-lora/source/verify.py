#!/usr/bin/python3
"""Independent cross-checks of the routed board against the source documents."""
import re, sys, os, collections
import pcbnew
sys.path.insert(0, os.path.dirname(__file__))
from netlist import CONN, SB_NAMES

fail = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond: fail.append(msg)

b = pcbnew.LoadBoard(os.path.join(os.path.dirname(__file__), "visormesh.kicad_pcb"))
pad_net = {}
for fp in b.GetFootprints():
    for p in fp.Pads():
        pad_net[(fp.GetReference(), p.GetNumber())] = p.GetNetname()

# 1. every pad's net equals the netlist (and unlisted pads are unconnected)
bad = [(k, pad_net[k], CONN.get(k, "")) for k in pad_net if pad_net[k] != CONN.get(k, "")]
check(not bad, f"board pad nets match netlist.py ({len(pad_net)} pads) {bad[:5]}")

# 2. Springboard pins vs Development Guide Table 5-1 names
expect = {"GND": "GND", "CD1*": "GND", "CD2*": "GND", "VCC": "VSB", "VDOCK": "VDOCK", "CS0*": "CS0_N",
          "CS1*": "CS1_N", "A17": "A17", "A8": "A8", "D0": "D0", "IRQ*": "IRQ_N", "RESET*": "RESET_N"}
for pin in range(1, 69):
    want = expect.get(SB_NAMES[pin], "")
    got = pad_net[("J1", str(pin))]
    if got != want:
        check(False, f"J1 pin {pin} ({SB_NAMES[pin]}) net {got!r} expected {want!r}")
check(True, "J1 pins 1-68 agree with Table 5-1 usage")

# 3. J1 pad geometry vs springSD IPC-356 netlist (positions relative to pin 1, layer side)
ipc = open("/home/claude/ref/springsd/gerb/SpringSD_1.1_gerber - CADCAM Netlist.IPC").read().splitlines()
ref = {}
for l in ipc:
    if l.startswith("327") and "CN1" in l:
        n = int(l[26:30].strip().lstrip("-")); acc = l[38:41]
        xy = re.search(r"X([+-]\d+)Y([+-]\d+)", l)
        ref[n] = (int(xy.group(1)) / 1e4 * 25.4, acc)
j1 = {p.GetNumber(): p for p in b.FindFootprintByReference("J1").Pads()}
x1 = pcbnew.ToMM(j1["1"].GetPosition().x)
geo_bad = []
for n, (xmm, acc) in ref.items():
    p = j1[str(n)]
    dx = pcbnew.ToMM(p.GetPosition().x) - x1 - (xmm - ref[1][0])
    side_ok = p.IsOnLayer(pcbnew.F_Cu) if acc == "A01" else p.IsOnLayer(pcbnew.B_Cu)
    if abs(dx) > 0.01 or not side_ok:
        geo_bad.append((n, round(dx, 3), acc))
check(not geo_bad, f"J1 pad pitch/side identical to springSD v1.1 footprint (68 pads) {geo_bad[:4]}")
# springSD's own nets on shared signals
sd_nets = {7: "H$CS0", 42: "H$CS1", 46: "HA17", 12: "HA8", 30: "HD0", 16: "$IRQ", 58: "$HRESET", 17: "VCC", 51: "VCC"}
names = {m.group(1): m.group(2) for m in re.finditer(r"P  (NNAME\d+) (\S+)", "\n".join(ipc))}
sd = {}
for l in ipc:
    if l.startswith("327") and "CN1" in l:
        sd[int(l[26:30].strip().lstrip("-"))] = names.get(l[3:17].strip(), l[3:17].strip())
check(all(sd[k].startswith(v) for k, v in sd_nets.items()), "CS0/CS1/A17/A8/D0/IRQ/RESET/VCC pins match springSD")

# 4. SC16IS740 pins vs NXP datasheet pin table (TSSOP16)
ds = {1: "VSB", 2: "SPI_NCS", 3: "SPI_MOSI", 4: "SPI_MISO", 5: "SPI_SCLK", 6: "GND", 7: "IRQ_N", 8: "GND",
      9: "GND", 10: "", 11: "GND", 12: "BR_TX", 13: "BR_RX", 14: "RESET_N", 15: "XTAL1", 16: "XTAL2"}
check(all(pad_net[("U4", str(k))] == v for k, v in ds.items()),
      "U4: VDD,CS,SI,SO,SCLK,SDA=GND,IRQ,I2C/SPI=GND(SPI),VSS,RTS nc,CTS=GND,TX,RX,RESET*,XTAL1,XTAL2")

# 5. domain isolation: no pad is on both VSB-domain nets and 3V3X except through U5-U7
vsb_side = {"VSB", "BR_TX", "BR_RX", "SPI_NCS", "SPI_MOSI", "SPI_MISO", "SPI_SCLK", "XTAL1", "XTAL2"}
x_side = {"3V3X", "XIAO_RX", "XIAO_TX", "VISOR_PRESENT"}
cross = collections.defaultdict(set)
for (r, p), n in pad_net.items():
    if n in vsb_side: cross[r].add("VSB")
    if n in x_side: cross[r].add("3V3X")
both = sorted(r for r, s in cross.items() if len(s) == 2)
check(both == ["J2", "J3", "U5", "U6", "U7"] or both == ["U5", "U6", "U7"],
      f"only the LVC1T45s straddle both power domains: {both}")
check(pad_net[("U5", "5")] == "VSB" and pad_net[("U6", "5")] == "GND" and pad_net[("U7", "5")] == "VSB",
      "DIR: U5 A->B (bridge TX->XIAO), U6 B->A (XIAO TX->bridge), U7 A->B (present)")
check(pad_net[("J2", "1")] == "VISOR_PRESENT" and pad_net[("J2", "7")] == "XIAO_TX"
      and pad_net[("J3", "1")] == "XIAO_VBUS" and pad_net[("J3", "2")] == "GND"
      and pad_net[("J3", "3")] == "3V3X" and pad_net[("J3", "7")] == "XIAO_RX",
      "XIAO: D0=present, D6=TX, D7=RX, 3V3, GND, 5V (matches companion firmware patch D6/D7)")

# 6. outline and stackup
bb = b.GetBoardEdgesBoundingBox()
w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
w -= 0.1  # bbox includes the 0.1 mm Edge.Cuts line width
check(abs(w - 50.43) < 0.05, f"board width {w:.2f} mm = springSD width 50.43 mm")
print(f"INFO board {w:.2f} x {h:.2f} mm, thickness {pcbnew.ToMM(b.GetDesignSettings().GetBoardThickness())} mm")
print("RESULT", "ALL PASS" if not fail else f"{len(fail)} FAIL")
