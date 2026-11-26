#!/usr/bin/env python3
"""Write rev 2's KiCad project file: net classes and design rules (KiCad keeps them in .kicad_pro).
Based on rev 1's project file. Rules are within JLCPCB's 4-layer standard capabilities."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]
p = json.load(open(os.path.join(HERE, "..", "kicad", "visormesh.kicad_pro")))
p["meta"]["filename"] = os.path.basename(out)
ns = p["net_settings"]
ns["netclass_patterns"] = [{"netclass": "Power", "pattern": n}
                           for n in ("GND", "VSB", "3V3X", "VDOCK", "VDOCK_D", "XIAO_VBUS", "VBAT")]
for c in ns["classes"]:
    if c["name"] == "Default":
        c.update({"track_width": 0.15, "clearance": 0.15, "via_diameter": 0.5, "via_drill": 0.25})
    if c["name"] == "Power":
        c.update({"track_width": 0.3, "clearance": 0.15, "via_diameter": 0.6, "via_drill": 0.3})
r = p["board"]["design_settings"]["rules"]
r.update({"min_clearance": 0.15, "min_track_width": 0.15, "min_via_diameter": 0.45,
          "min_through_hole_diameter": 0.25, "min_hole_clearance": 0.25, "min_copper_edge_clearance": 0.3,
          "min_text_height": 0.8})
json.dump(p, open(out, "w"), indent=2)
# Rev 1's rule: J1's straddle pads run to the board edge by design (the connector clamps the edge).
open(os.path.splitext(out)[0] + ".kicad_dru", "w").write(open(os.path.join(HERE, "..", "kicad", "visormesh.kicad_dru")).read())
