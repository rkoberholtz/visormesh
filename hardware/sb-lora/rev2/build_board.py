#!/usr/bin/env python3
"""Generate the unrouted rev 2 board with KiCad's pcbnew API (KiCad 10). Run with KiCad's own Python.

Same frame as rev 1 (../source/build_board.py): springSD's in-slot outline, connector edge at the
bottom, the extension for the radio boards at the top. 4 layers: F.Cu, In2.Cu and B.Cu carry
signals; In1.Cu is a solid GND plane.

J1 is a Robinson Nugent / 3M MCS-068SMA1-TR30 straddle receptacle: pins 1-34 on F.Cu, 35-68 on
B.Cu directly opposite, 1.27 mm pitch. The pads are rev 1's, which the part fits. Rev 1's 1.6 mm
board is too thick for it, so BOARD_T must be set from the gap between its rows before ordering.
Keep the J1 zone (J1_KEEP from the edge, both sides) free of other parts.
"""
import sys, os
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from netlist import PARTS, CONN, NETS, SB_NAMES, XIAO_NAMES

MM = pcbnew.FromMM
IN = 25.4
OX, OY = 100.0, 100.0          # springSD origin (inches, y-up) mapped to KiCad (mm, y-down)
def sb(x_in, y_in):
    return OX + x_in * IN, OY - y_in * IN
def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))

FPLIB = os.environ.get("KICAD_FOOTPRINTS",
    "/opt/homebrew/Caskroom/kicad/10.0.8/KiCad/KiCad.app/Contents/SharedSupport/footprints")
LOCAL = os.path.join(HERE, "visormesh.pretty")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "visormesh.kicad_pcb")
REV = "v0.2"

# ---- J1 straddle land pattern: rev 1's pads, checked against an MCS-068SMA1 on a rev 1 board ----
J1_PITCH = 1.27       # contact pitch within a row (catalogue: .050")
J1_PAD = (0.7, 1.83)  # pad width along the edge, length into the board (rev 1)
J1_ROW = J1_PAD[1] / 2 + 0.01  # pads run from the board edge inward, as on rev 1 (10 um in, so DRC sees no contact)
J1_KEEP = 5.5         # no other parts within this distance of the edge, top or bottom
# J1 body (measured): 47.5 mm across the ears, 44.5 mm without; each ear has a hole 1.0 wide, 1.8 high
# and 1.5 deep facing the board, centred in the ear. The board takes the ears' width, centred
# on J1, with a tab into each ear hole so the connector is located on the board.
J1_EAR_W, J1_BODY_W = 47.5, 44.5
J1_HOLE = (1.0, 1.5)    # ear hole width, depth
TAB_CLR = 0.1           # clearance around the tabs in the holes
NOTCH = 0.6             # relief cut beside each tab, so the router's 0.5 mm corner radius clears the ear
BOARD_T = 0.8         # the MCS-068SMA1 tails measure 1.0 mm outside, so the gap is a little less; err thin

board = pcbnew.BOARD()
board.SetCopperLayerCount(4)
board.SetLayerType(pcbnew.In1_Cu, pcbnew.LT_POWER)
ds = board.GetDesignSettings()
ds.SetBoardThickness(MM(BOARD_T))
ds.m_TrackMinWidth = MM(0.15)
ds.m_MinClearance = MM(0.15)
ds.m_ViasMinSize = MM(0.45)
ds.m_MinThroughDrill = MM(0.25)
ds.m_CopperEdgeClearance = MM(0.3)
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(MM(0.15)); nc.SetTrackWidth(MM(0.15)); nc.SetViaDiameter(MM(0.5)); nc.SetViaDrill(MM(0.25))
POWER_NETS = ("GND", "VSB", "3V3X", "VDOCK", "VDOCK_D", "XIAO_VBUS", "VBAT")

# ---------------- outline ----------------
J1_CX = OX + 0.8 * IN - 33 * J1_PITCH / 2               # J1's centre line
HOLE_OUT = J1_EAR_W / 2 - ((J1_EAR_W - J1_BODY_W) / 2 - J1_HOLE[0]) / 2   # hole's outer side from J1_CX
TAB_OUT, TAB_IN = HOLE_OUT - TAB_CLR, HOLE_OUT - J1_HOLE[0] + TAB_CLR
TAB_L = J1_HOLE[1] - 2 * TAB_CLR
X0, X1 = J1_CX - TAB_OUT, J1_CX + TAB_OUT               # outer edges flush with the tabs' outer sides
Y_SLOT = sb(0, 1.060307)[1]         # top of springSD outline = slot opening
# A Memplug module, flush with the slot opening when seated, has its connector face (flush with its
# case end) 56.8 mm in. J1's face sits J1_D in front of the board edge. springSD's edge was 1.15 mm short.
SLOT_TO_FACE, J1_D = 56.8, 5.2
Y_EDGE = round(Y_SLOT + SLOT_TO_FACE - J1_D, 3)   # connector edge (bottom)
EXT = 24.6                          # extension beyond the slot for the XIAO and Wio
Y_TOP = Y_SLOT - EXT
def side(sx):        # one tab and its relief notch, from the outer edge inward (sx = +1 right, -1 left)
    c = J1_CX
    return [(c + sx * TAB_OUT, Y_EDGE + TAB_L), (c + sx * TAB_IN, Y_EDGE + TAB_L),
            (c + sx * TAB_IN, Y_EDGE - NOTCH), (c + sx * (TAB_IN - 1.0), Y_EDGE - NOTCH),
            (c + sx * (TAB_IN - 1.0), Y_EDGE)]
pts = [(X0, Y_TOP), (X1, Y_TOP)] + side(1) + side(-1)[::-1]
for i in range(len(pts)):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*pts[i])); s.SetEnd(V(*pts[(i + 1) % len(pts)])); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)
for lyr in (pcbnew.F_Fab, pcbnew.B_Fab, pcbnew.F_SilkS):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(X0 + 1, Y_SLOT)); s.SetEnd(V(X1 - 1, Y_SLOT)); s.SetLayer(lyr); s.SetWidth(MM(0.12))
    board.Add(s)
# J1 zone on both fab layers, so the connector's extent shows on the assembly drawings
for lyr in (pcbnew.F_Fab, pcbnew.B_Fab):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_RECT)
    s.SetStart(V(X0 + 0.5, Y_EDGE - J1_KEEP)); s.SetEnd(V(X1 - 0.5, Y_EDGE - 0.2))
    s.SetLayer(lyr); s.SetWidth(MM(0.1)); board.Add(s)

# ---------------- nets ----------------
netinfo = {}
for n in NETS:
    ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); netinfo[n] = ni

def springboard_fp():
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("visormesh", "SPRINGBOARD_68_STRADDLE"))
    fp.SetReference("J1"); fp.SetValue("MCS-068SMA1 straddle")
    for n in range(1, 69):
        top = n <= 34       # same split as rev 1: pin n on F.Cu, pin n + 34 opposite it on B.Cu
        k = (n - 1) if top else (n - 35)
        x = 0.8 * IN - k * J1_PITCH
        y = -J1_ROW
        p = pcbnew.PAD(fp)
        p.SetNumber(str(n)); p.SetShape(pcbnew.PAD_SHAPE_RECT); p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetSize(V(*J1_PAD))
        ls = pcbnew.LSET()
        for l in ((pcbnew.F_Cu, pcbnew.F_Mask, pcbnew.F_Paste) if top else (pcbnew.B_Cu, pcbnew.B_Mask, pcbnew.B_Paste)):
            ls.AddLayer(l)
        p.SetLayerSet(ls)
        fp.Add(p)
        p.SetPosition(V(OX + x, Y_EDGE + y))
    return fp

placed = {}
def load(ref):
    val, lib, name, lcsc, desc, dnp = PARTS[ref]
    if ref == "J1":
        fp = springboard_fp()
    else:
        path = LOCAL if lib == "visormesh" else os.path.join(FPLIB, lib + ".pretty")
        fp = pcbnew.FootprintLoad(path, name)
        assert fp, (ref, lib, name)
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref); fp.SetValue(val)
    if lcsc:
        fp.SetField("LCSC", lcsc)
        fp.GetField("LCSC").SetVisible(False)
    board.Add(fp)
    placed[ref] = fp
    return fp

def place(ref, x, y, rot=0.0, back=False):
    fp = load(ref)
    if ref != "J1":
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
    if back:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    return fp

# ---- placement (KiCad mm). Slot zone: Y_SLOT..Y_EDGE. Extension: Y_TOP..Y_SLOT ----
# Slot pin k of row A sits at x = 120.32 - 1.27(k-1): D3-D7 at 119-114, OE* 110.2, WE* 102.5,
# IRQ* 101.3, VCC 100, A1-A3 88.6-86, D0-D2 83.5-81.
place("J1", 0, 0)
place("U1", 101.0, 104.5, 0)     # flash: address pins left, data right
place("C2", 113.5, 104.5, 90)    # U1 VCC (pin 37, right side)
place("JP2", 101.0, 95.5, 0)     # WE* -> flash WE#
place("R4", 105.5, 95.5, 90)
place("R5", 96.5, 92.5, 90)      # UART INT pull-down, beside U4
place("U2", 83.0, 89.0, 0)       # UART near D0-D2 and A1-A3
place("C3", 83.0, 84.0, 0)
place("Y1", 83.0, 98.0, 0)
place("C6", 79.0, 98.0, 90)
place("C7", 87.0, 98.0, 90)
place("U3", 114.5, 92.5, 0)      # OR strobes near OE*/CS1*/WE*
place("C4", 114.5, 89.0, 0)
place("U4", 93.0, 92.5, 0)       # INT/RESET inverters
place("C5", 93.0, 89.0, 0)
place("C1", 92.0, 114.0, 0)     # VSB bulk at VCC pins 17/51
# Ioff translators near the slot opening, where the XIAO-side nets cross
place("U5", 95.0, 78.5, 0)
place("C8", 95.0, 81.5, 0); place("C9", 95.0, 75.5, 0)
place("U6", 101.0, 78.5, 0)
place("C10", 101.0, 81.5, 0); place("C11", 101.0, 75.5, 0)
place("R1", 90.5, 78.5, 90)      # XIAO RX pull-up (3V3X)
place("R2", 105.0, 78.5, 90)     # UART RX pull-up (VSB)
place("R3", 88.0, 78.5, 90)      # Visor-present pull-down
# VDOCK path
place("D1", 112.0, 78.5, 90)
place("JP1", 116.5, 78.5, 90)
# Radio boards side by side, USB-C and pin 1 toward the outer edge
YX = Y_TOP + 11.0
place("X1", 87.4, YX, 0)
place("X2", 112.5, YX, 0)
place("C12", 100.25, YX + 7.5, 90)   # 3V3X bulk between the boards
place("TP1", 100.25, Y_TOP + 2.5); place("TP2", 100.25, Y_TOP + 5.3)
place("TP3", 100.25, Y_TOP + 8.1); place("TP4", 100.25, Y_TOP + 10.9)
place("BT1", 100.25, YX + 0.5, 90, back=True)   # LiPo under the boards

# ---------------- reference text ----------------
for ref, fp in placed.items():
    fp.Value().SetVisible(False)
    r = fp.Reference()
    if ref in ("JP1", "JP2", "TP1", "TP2", "TP3", "TP4"):
        r.SetTextSize(V(0.8, 0.8)); r.SetTextThickness(MM(0.12))
    else:
        r.SetLayer(pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab)
        r.SetMirrored(fp.IsFlipped())
        r.SetTextSize(V(0.5, 0.5)); r.SetTextThickness(MM(0.08))
    for t in fp.GraphicalItems():       # the XIAO footprint's pad labels are under 0.8 mm
        if t.GetClass() == "PCB_TEXT" and t.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) \
                and pcbnew.ToMM(t.GetTextHeight()) < 0.8:
            t.SetTextSize(V(0.8, 0.8)); t.SetTextThickness(MM(0.12))
        if t.GetClass() == "PCB_TEXT" and t.GetLayer() == pcbnew.B_SilkS:
            t.SetMirrored(True)
placed["JP1"].Reference().SetPosition(V(118.8, 78.5))
placed["JP2"].Reference().SetPosition(V(101.0, 93.8))
for i, ref in enumerate(("TP1", "TP2", "TP3", "TP4")):
    placed[ref].Reference().SetPosition(V(97.6, Y_TOP + 2.5 + 2.8 * i))
    placed[ref].Reference().SetVisible(False)

# ---------------- assign nets ----------------
for (ref, pad), net in CONN.items():
    hit = [p for p in placed[ref].Pads() if p.GetNumber() == pad]
    assert hit, (ref, pad)
    for p in hit:
        p.SetNet(netinfo[net])

# ---------------- GND fanout ----------------
# The router doesn't drop vias from pads to a plane, so each SMD GND pad gets a via to In1 here,
# just outside the pad, away from the part's centre. Locked, so finish_board.py keeps them.
VIA_D, VIA_DRILL, GAP = 0.5, 0.25, 0.2
def fanout(pad, dx, dy, dist=None, via=True):
    px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    sx, sy = pcbnew.ToMM(pad.GetBoundingBox().GetWidth()), pcbnew.ToMM(pad.GetBoundingBox().GetHeight())
    if dist is None:
        dist = (sx if dx else sy) / 2 + VIA_D / 2 + GAP
    vx, vy = px + dx * dist, py + dy * dist
    lyr = pcbnew.B_Cu if pad.IsOnLayer(pcbnew.B_Cu) and not pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.F_Cu
    t = pcbnew.PCB_TRACK(board); t.SetLayer(lyr); t.SetWidth(MM(0.3)); t.SetNet(netinfo["GND"])
    t.SetStart(V(px, py)); t.SetEnd(V(vx, vy)); t.SetLocked(True); board.Add(t)
    if not via:
        return
    v = pcbnew.PCB_VIA(board); v.SetNet(netinfo["GND"]); v.SetPosition(V(vx, vy))
    v.SetWidth(MM(VIA_D)); v.SetDrill(MM(VIA_DRILL)); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetLocked(True); board.Add(v)

for ref, fp in placed.items():
    c = fp.GetPosition()
    for p in fp.Pads():
        if p.GetNetname() != "GND" or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        n = p.GetNumber()
        if ref == "J1":
            # Inward, since the tabs' relief notches are beside the end pads. A top-row pad with GND
            # opposite shares that pad's via.
            row_a = int(n) <= 34
            shared = row_a and CONN.get(("J1", str(int(n) + 34))) == "GND"
            fanout(p, 0, -1, via=not shared)
            continue
        if ref in ("U2", "BT1"):
            continue        # U2: through its exposed pad, below. BT1 is on B.Cu, in the B.Cu GND pour
        ox, oy = p.GetPosition().x - c.x, p.GetPosition().y - c.y
        if ox == 0 and oy == 0:
            fanout(p, 1, 0)             # single-pad test point
            continue
        if abs(ox) >= abs(oy):
            fanout(p, 1 if ox > 0 else -1, 0)
        else:
            fanout(p, 0, 1 if oy > 0 else -1)
# U2: GND pins 7, 13, 16, 25 join the exposed pad on F.Cu; the pad gets four vias to the plane
u2 = placed["U2"]; ep = [p for p in u2.Pads() if p.GetNumber() == "33"][0]
ec = ep.GetPosition()
for p in u2.Pads():
    if p.GetNumber() in ("7", "13", "16", "25"):
        pp = p.GetPosition()          # straight in from the pin, so it stays in its own lane
        lim = MM(1.5)                 # stay on the 3.45 mm exposed pad
        clamp = lambda v, c: max(c - lim, min(c + lim, v))
        end = (pcbnew.VECTOR2I(ec.x, clamp(pp.y, ec.y)) if abs(pp.x - ec.x) > abs(pp.y - ec.y)
               else pcbnew.VECTOR2I(clamp(pp.x, ec.x), ec.y))
        t = pcbnew.PCB_TRACK(board); t.SetLayer(pcbnew.F_Cu); t.SetWidth(MM(0.2)); t.SetNet(netinfo["GND"])
        t.SetStart(pp); t.SetEnd(end); t.SetLocked(True); board.Add(t)
for dx, dy in ((-0.75, -0.75), (0.75, -0.75), (-0.75, 0.75), (0.75, 0.75)):
    v = pcbnew.PCB_VIA(board); v.SetNet(netinfo["GND"])
    v.SetPosition(pcbnew.VECTOR2I(ec.x + MM(dx), ec.y + MM(dy)))
    v.SetWidth(MM(VIA_D)); v.SetDrill(MM(VIA_DRILL)); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetLocked(True); board.Add(v)

# ---------------- GND plane on In1 (the router treats it as a plane) ----------------
z = pcbnew.ZONE(board); z.SetLayer(pcbnew.In1_Cu); z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS); z.SetNet(netinfo["GND"])
ol = z.Outline(); ol.NewOutline()
for x, y in ((X0, Y_TOP), (X1, Y_TOP), (X1, Y_EDGE), (X0, Y_EDGE)):
    ol.Append(MM(x), MM(y))
z.SetLocalClearance(MM(0.25)); z.SetMinThickness(MM(0.2))
z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
z.SetThermalReliefGap(MM(0.3)); z.SetThermalReliefSpokeWidth(MM(0.3))
board.Add(z)

# Keep-out strip along the connector edge, so the router doesn't pass between J1's pads and the edge
ka = pcbnew.ZONE(board); ka.SetIsRuleArea(True)
ls = pcbnew.LSET(); [ls.AddLayer(l) for l in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)]
ka.SetLayerSet(ls)
ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
ka.SetDoNotAllowFootprints(False); ka.SetDoNotAllowZoneFills(False)
ol = ka.Outline(); ol.NewOutline()
ky = Y_EDGE - J1_ROW + J1_PAD[1] / 2 - 0.1      # just inside the pad tips
for x, y in ((X0, ky), (X1, ky), (X1, Y_EDGE), (X0, Y_EDGE)):
    ol.Append(MM(x), MM(y))
board.Add(ka)

# ---------------- silkscreen ----------------
def silk(s, x, y, size=0.8, layer=pcbnew.F_SilkS, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(V(size, size)); t.SetTextThickness(MM(size * 0.15))
    if mirror: t.SetMirrored(True)
    board.Add(t)
    return t

silk("VisorMesh SB-LoRa " + REV, 100.0, 112.0, 1.0)
silk("^ SLOT OPENING ^", 84.0, Y_SLOT + 1.3, 0.8)
silk("USB-C this edge", 87.0, Y_TOP + 1.0, 0.8, pcbnew.B_SilkS, True)
silk("1", OX + 0.8 * IN, Y_EDGE - J1_KEEP - 0.8, 0.8)
silk("34", OX - 0.85 * IN, Y_EDGE - J1_KEEP - 0.8, 0.8)
silk("35", OX + 0.8 * IN, Y_EDGE - J1_KEEP - 0.8, 0.8, pcbnew.B_SilkS, True)
silk("68", OX - 0.85 * IN, Y_EDGE - J1_KEEP - 0.8, 0.8, pcbnew.B_SilkS, True)
silk("LiPo", 100.25, YX + 4.3, 0.8, pcbnew.B_SilkS, True)
for i, lab in enumerate(("IO", "CK", "RS", "G")):     # SWDIO, SWCLK, NRST, GND
    silk(lab, 102.5, Y_TOP + 2.5 + 2.8 * i, 0.8)
silk("VDOCK", 116.5, 75.3, 0.8)
silk("WE", 101.0, 97.3, 0.8)

board.Save(OUT)
print("saved", OUT, "slot", round(Y_SLOT, 3), "top", round(Y_TOP, 3), "edge", round(Y_EDGE, 3),
      "x", round(X0, 3), round(X1, 3))
