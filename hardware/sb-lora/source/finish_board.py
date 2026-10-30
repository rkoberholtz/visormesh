#!/usr/bin/python3
"""Import Freerouting session, add GND pours, fill, save, and write custom DRC rules."""
import sys, pcbnew
MM = pcbnew.FromMM
pcb = sys.argv[1]; ses = sys.argv[2]
b = pcbnew.LoadBoard(pcb)
import re
def sexp(txt):
    toks = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', txt)
    st = [[]]
    for t in toks:
        if t == "(": st.append([])
        elif t == ")": e = st.pop(); st[-1].append(e)
        else: st[-1].append(t.strip('"'))
    return st[0][0]
S = sexp(open(ses).read())
def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]
routes = find(S, "routes")[0]
res = float(find(routes, "resolution")[0][2])          # units per um
k = 1.0 / (res * 1000.0)                                # -> mm
NM = lambda v: int(round(float(v) * 1000.0 / res))      # session units -> KiCad nm, exact
for remove in [t for t in b.GetTracks()]:
    b.Remove(remove)
ntr = nvia = 0
for net in find(find(routes, "network_out")[0], "net"):
    ni = b.FindNet(net[1])
    for w in find(net, "wire"):
        path = find(w, "path")[0]
        layer = b.GetLayerID(path[1]); width = float(path[2]) * k
        xy = path[3:]
        pts = [(NM(xy[i]), -NM(xy[i + 1])) for i in range(0, len(xy), 2)]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(b); t.SetLayer(layer); t.SetWidth(MM(width)); t.SetNet(ni)
            t.SetStart(pcbnew.VECTOR2I(ax, ay)); t.SetEnd(pcbnew.VECTOR2I(bx, by))
            b.Add(t); ntr += 1
    for v in find(net, "via"):
        m = re.search(r'_(\d+):(\d+)_um', v[1]); dia, drl = int(m.group(1)) / 1000, int(m.group(2)) / 1000
        via = pcbnew.PCB_VIA(b); via.SetNet(ni)
        via.SetPosition(pcbnew.VECTOR2I(NM(v[2]), -NM(v[3])))
        via.SetWidth(MM(dia)); via.SetDrill(MM(drl)); via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        b.Add(via); nvia += 1
print("imported", ntr, "segments", nvia, "vias")
gnd = b.FindNet("GND")
bb = b.GetBoardEdgesBoundingBox()
x0, y0, x1, y1 = bb.GetX(), bb.GetY(), bb.GetRight(), bb.GetBottom()
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(gnd)
    ol = z.Outline(); ol.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        ol.Append(x, y)
    z.SetLocalClearance(MM(0.25)); z.SetMinThickness(MM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
    z.SetThermalReliefGap(MM(0.3)); z.SetThermalReliefSpokeWidth(MM(0.3))
    z.SetIsFilled(False)
    b.Add(z)
# drop dangling stubs / orphan vias left by the router
def ends(t): return (t.GetStart(), t.GetEnd())
def key(t):
    a, c = t.GetStart(), t.GetEnd()
    a, c = sorted([(a.x, a.y), (c.x, c.y)])
    return (t.GetLayer(), t.GetNetname(), a, c)
seen = set()
for t in [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]:
    if key(t) in seen or t.GetStart() == t.GetEnd():
        b.Remove(t)
    else:
        seen.add(key(t))
for _ in range(10):
    tracks = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
    removed = 0
    for t in list(tracks):
        if t.GetNetname() == "GND":
            continue
        for pt in ends(t):
            touch = any(o is not t and o.GetLayer() == t.GetLayer() and pt in ends(o) for o in tracks)
            touch = touch or any(v.GetPosition() == pt for v in vias)
            touch = touch or any(p.HitTest(pt) and p.IsOnLayer(t.GetLayer()) for p in b.GetPads())
            if not touch:
                b.Remove(t); removed += 1; break
    for v in list(vias):
        n = sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and v.GetPosition() in ends(t))
        if n == 0:
            b.Remove(v); removed += 1
    if not removed: break
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(pcb)
print("tracks", len(b.GetTracks()), "zones", len(b.Zones()))
