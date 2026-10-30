#!/usr/bin/env python3
"""Write net-class patterns and fab rules into the KiCad project (KiCad 7 keeps them in .kicad_pro)."""
import json, sys
f = sys.argv[1]
p = json.load(open(f))
ns = p.setdefault("net_settings", {})
ns["netclass_patterns"] = [{"netclass": "Power", "pattern": n}
                           for n in ("GND", "VSB", "3V3X", "VDOCK", "VDOCK_D", "XIAO_VBUS")]
for c in ns["classes"]:
    if c["name"] == "Power":
        c.update({"track_width": 0.4, "clearance": 0.2, "via_diameter": 0.7, "via_drill": 0.35})
    if c["name"] == "Default":
        c.update({"track_width": 0.2, "clearance": 0.2, "via_diameter": 0.6, "via_drill": 0.3})
json.dump(p, open(f, "w"), indent=2)
