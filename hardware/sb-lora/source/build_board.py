#!/usr/bin/python3
"""Generate visormesh.kicad_pcb (unrouted) with KiCad 7's pcbnew API. Run with /usr/bin/python3."""
import sys, os
import pcbnew
sys.path.insert(0, os.path.dirname(__file__))
from netlist import PARTS, CONN, NETS, SB_NAMES, XIAO_NAMES

MM = pcbnew.FromMM
IN = 25.4
OX, OY = 100.0, 100.0          # springSD origin (inches, y-up) mapped to KiCad (mm, y-down)
def sb(x_in, y_in):            # springSD coordinate -> KiCad mm
    return OX + x_in * IN, OY - y_in * IN
def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))

FPLIB = "/usr/share/kicad/footprints"
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "visormesh.kicad_pcb")

board = pcbnew.BOARD()
board.SetCopperLayerCount(2)
ds = board.GetDesignSettings()
ds.SetBoardThickness(MM(1.6))
ds.m_TrackMinWidth = MM(0.15)
ds.m_MinClearance = MM(0.15)
ds.m_ViasMinSize = MM(0.6)
ds.m_MinThroughDrill = MM(0.3)
ds.m_CopperEdgeClearance = MM(0.3)
nc = ds.m_NetSettings.m_DefaultNetClass
nc.SetClearance(MM(0.2)); nc.SetTrackWidth(MM(0.2)); nc.SetViaDiameter(MM(0.6)); nc.SetViaDrill(MM(0.3))
pwr = pcbnew.NETCLASS("Power")
pwr.SetClearance(MM(0.2)); pwr.SetTrackWidth(MM(0.4)); pwr.SetViaDiameter(MM(0.7)); pwr.SetViaDrill(MM(0.35))
ds.m_NetSettings.m_NetClasses["Power"] = pwr
POWER_NETS = ("GND", "VSB", "3V3X", "VDOCK", "VDOCK_D", "XIAO_VBUS")

# ---------------- outline ----------------
X0, X1 = sb(-0.991, 0)[0], sb(0.994315, 0)[0]
Y_EDGE = sb(0, -0.926)[1]           # connector edge (bottom)
Y_SLOT = sb(0, 1.060307)[1]         # top of springSD outline = slot opening
EXT = 24.6                          # extension beyond slot for the XIAO stack
Y_TOP = Y_SLOT - EXT
pts = [(X0, Y_TOP), (X1, Y_TOP), (X1, Y_EDGE), (X0, Y_EDGE)]
for i in range(4):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*pts[i])); s.SetEnd(V(*pts[(i + 1) % 4])); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)
# slot-opening reference line on both fab layers
for lyr in (pcbnew.F_Fab, pcbnew.B_Fab, pcbnew.F_SilkS):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(X0 + 1, Y_SLOT)); s.SetEnd(V(X1 - 1, Y_SLOT)); s.SetLayer(lyr); s.SetWidth(MM(0.12))
    board.Add(s)

# ---------------- nets ----------------
netinfo = {}
for n in NETS:
    ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); netinfo[n] = ni
    if n in POWER_NETS:
        ni.SetNetClass(pwr)

# ---------------- footprints ----------------
def text(fp, layer, s, x, y, size=0.8):
    t = pcbnew.PCB_TEXT(fp) if hasattr(pcbnew, "PCB_TEXT") else pcbnew.FP_TEXT(fp)
    return t

def springboard_fp():
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("visormesh", "SPRINGBOARD_68_STRADDLE"))
    fp.SetReference("J1"); fp.SetValue("Springboard 68p")
    pad_len = 1.83                  # from board edge inward; contact lands ~0.66 mm from edge
    for n in range(1, 69):
        top = n <= 34
        k = (n - 1) if top else (n - 35)
        x = (0.8 - k * 0.05) * IN     # local to footprint origin (OX, Y_EDGE - pad_len/2)
        y = 0.0
        p = pcbnew.PAD(fp)
        p.SetNumber(str(n)); p.SetShape(pcbnew.PAD_SHAPE_RECT); p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetSize(V(0.7, pad_len))
        ls = pcbnew.LSET()
        if top:
            ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.F_Mask)
        else:
            ls.AddLayer(pcbnew.B_Cu); ls.AddLayer(pcbnew.B_Mask)
        p.SetLayerSet(ls)
        fp.Add(p)
        p.SetPos0(V(x, y)) if hasattr(p, "SetPos0") else None
        p.SetPosition(V(x, y))
    fp.SetPosition(V(OX, Y_EDGE - pad_len / 2))
    return fp

placed = {}
def load(ref):
    val, lib, name, lcsc, desc, dnp = PARTS[ref]
    if ref == "J1":
        fp = springboard_fp()
    else:
        fp = pcbnew.FootprintLoad(os.path.join(FPLIB, lib + ".pretty"), name)
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref); fp.SetValue(val)
    if lcsc:
        fp.SetProperty("LCSC", lcsc)
    board.Add(fp)
    placed[ref] = fp
    return fp

def place(ref, x, y, rot=0.0, back=False):
    fp = placed.get(ref) or load(ref)
    if ref != "J1":
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
    if back:
        fp.Flip(fp.GetPosition(), False)
    return fp

# ---- placement (KiCad mm). Slot zone: Y_SLOT..Y_EDGE. Extension: Y_TOP..Y_SLOT ----
XC = OX
place("J1", 0, 0)
# Bus decode near the connector signals: CS0* pin7 (x=112.7), A8 pin12 (106.35), CS1* pin42 (111.43), A17 pin46 (106.35)
place("U1", 107.0, 113.5, 0)    # A17 latch
place("U2", 112.0, 113.5, 0)    # A8 latch
place("U3", 117.0, 113.5, 0)    # CS1 inverter
place("C2", 107.0, 110.5, 0)
place("C3", 112.0, 110.5, 0)
place("C4", 117.0, 110.5, 0)
place("C1", 100.0, 116.5, 90)   # VSB bulk at VCC pins 17/51
# Bridge
place("U4", 100.0, 101.0, 0)
place("C5", 95.5, 96.5, 90)
place("Y1", 108.0, 101.5, 90)
place("C6", 108.0, 97.0, 0)
place("C7", 108.0, 106.0, 0)
place("R1", 89.0, 112.0, 90)    # D0 (pin30 @ x=83.49) -> SO
# Ioff translators near the slot opening
place("U5", 93.0, 83.0, 0)
place("U6", 100.0, 83.0, 0)
place("U7", 107.0, 83.0, 0)
place("C8", 90.5, 80.0, 90); place("C9", 95.5, 80.0, 90)
place("C10", 97.5, 80.0, 90); place("C11", 102.5, 80.0, 90)
place("C12", 104.5, 80.0, 90); place("C13", 109.5, 80.0, 90)
place("R3", 100.0, 89.0, 90)    # bridge RX pull-up (VSB)
place("R2", 89.0, 70.5, 90)     # XIAO RX pull-up (3V3X)
place("R4", 85.0, 52.5, 90)     # VISOR_PRESENT pull-down
place("C14", 113.0, 62.0, 90)   # 3V3X bulk next to XIAO 3V3
# VDOCK path
place("D1", 92.5, 116.0, 90)
place("JP1", 116.0, 54.0, 90)
# XIAO stack: centre (XC, YX), USB-C toward Y_TOP
YX = Y_SLOT - 3.0 - 10.5
place("J2", XC - 7.62, YX - 7.62, 0)    # XIAO pins 1..7 top->bottom (left column, top view)
place("J3", XC + 7.62, YX - 7.62, 0)    # XIAO pins 14..8 top->bottom (right column)

# ---------------- reference/value text ----------------
for ref, fp in placed.items():
    fp.Value().SetVisible(False)
    r = fp.Reference()
    if ref in ("J2", "J3", "JP1"):
        r.SetTextSize(V(0.8, 0.8)); r.SetTextThickness(MM(0.12))
    else:
        r.SetLayer(pcbnew.F_Fab)       # refs live on the assembly drawing
        r.SetTextSize(V(0.5, 0.5)); r.SetTextThickness(MM(0.08))
# U4 pin 8 (I2C/SPI select) sits between signal pins: solid-connect it to the pour
for p in placed["U4"].Pads():
    if p.GetNumber() in ("8", "9", "6", "11"):
        p.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
placed["J2"].Reference().SetPosition(V(XC - 7.62, YX + 7.62 + 2.6))
placed["J3"].Reference().SetPosition(V(XC + 7.62, YX + 7.62 + 2.6))
placed["JP1"].Reference().SetPosition(V(118.6, 54.0))
for p in placed["J1"].Pads():
    if p.GetNumber() in ("1", "34", "35", "36", "67", "68"):
        p.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

# ---------------- assign nets ----------------
for (ref, pad), net in CONN.items():
    fp = placed[ref]
    hit = [p for p in fp.Pads() if p.GetNumber() == pad]
    assert hit, (ref, pad)
    for p in hit:
        p.SetNet(netinfo[net])

# ---------------- silkscreen / fab notes ----------------
def silk(s, x, y, size=0.8, layer=pcbnew.F_SilkS, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(V(size, size)); t.SetTextThickness(MM(size * 0.15))
    if mirror: t.SetMirrored(True)
    board.Add(t)
    return t

t = silk("VisorMesh SB-LoRa v0.1", 77.4, 96.0, 1.0); t.SetTextAngleDegrees(90)
silk("^ SLOT OPENING ^", 84.0, Y_SLOT + 1.3, 0.8)
silk("XIAO USB-C this edge, parts up", XC, Y_TOP + 1.0, 0.8)
silk("1", OX + 0.8 * IN, Y_EDGE - 2.6, 0.8)
silk("34", OX - 0.85 * IN, Y_EDGE - 2.6, 0.8)
silk("35", OX + 0.8 * IN, Y_EDGE - 2.6, 0.8, pcbnew.B_SilkS, True)
silk("68", OX - 0.85 * IN, Y_EDGE - 2.6, 0.8, pcbnew.B_SilkS, True)
for k in range(1, 8):
    silk(XIAO_NAMES[k], XC - 7.62 - 2.9, YX - 7.62 + (k - 1) * 2.54, 0.8)
    silk(XIAO_NAMES[15 - k], XC + 7.62 + 3.0, YX - 7.62 + (k - 1) * 2.54, 0.8)
silk("VDOCK>5V", 121.0, 60.5, 0.8)
silk("keep open", 121.0, 61.7, 0.8)

board.Save(OUT)
print("saved", OUT, "slot", round(Y_SLOT, 3), "top", round(Y_TOP, 3), "edge", round(Y_EDGE, 3),
      "x", round(X0, 3), round(X1, 3))
