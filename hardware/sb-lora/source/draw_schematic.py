#!/usr/bin/env python3
"""Net-label schematic (PDF) generated from netlist.py: every pin shows its number, function and net."""
import sys, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle
from matplotlib.backends.backend_pdf import PdfPages
sys.path.insert(0, os.path.dirname(__file__))
from netlist import CONN, PARTS, SB_NAMES, XIAO_NAMES

PIN = {
    "U1": {1: "D", 2: "CP", 3: "GND", 4: "Q̅", 5: "VCC"},
    "U2": {1: "D", 2: "CP", 3: "GND", 4: "Q̅", 5: "VCC"},
    "U3": {1: "NC", 2: "A", 3: "GND", 4: "Y", 5: "VCC"},
    "U4": {1: "VDD", 2: "CS*", 3: "SI", 4: "SO", 5: "SCLK", 6: "SDA", 7: "IRQ*", 8: "I2C/SPI*",
           9: "VSS", 10: "RTS*", 11: "CTS*", 12: "TX", 13: "RX", 14: "RESET*", 15: "XTAL1", 16: "XTAL2"},
    "U5": {1: "VCCA", 2: "GND", 3: "A", 4: "B", 5: "DIR", 6: "VCCB"},
    "U6": {1: "VCCA", 2: "GND", 3: "A", 4: "B", 5: "DIR", 6: "VCCB"},
    "U7": {1: "VCCA", 2: "GND", 3: "A", 4: "B", 5: "DIR", 6: "VCCB"},
    "Y1": {1: "X1", 2: "GND", 3: "X2", 4: "GND"},
    "D1": {1: "K", 2: "A"}, "JP1": {1: "1", 2: "2"},
}
DOM = {"VSB": "#2b6cb0", "3V3X": "#b7791f"}

def pins_of(ref):
    return sorted({int(p) for (r, p) in CONN if r == ref} | set(PIN.get(ref, {}).keys()))

def block(ax, x, y, ref, title, pins, names, w=5.2, color="#333", desc=True):
    h = 0.42 * len(pins) + 0.9
    ax.add_patch(FancyBboxPatch((x, y - h), w, h, boxstyle="round,pad=0.02", fc="#fbfbfb", ec=color, lw=1.2))
    ax.text(x + 0.15, y - 0.35, f"{ref}  {title}", fontsize=8.5, weight="bold", va="center")
    ax.text(x + 0.15, y - 0.68, (PARTS[ref][4] if (ref in PARTS and desc) else "")[:58], fontsize=5.6, color="#666", va="center")
    for i, p in enumerate(pins):
        yy = y - 1.0 - i * 0.42
        net = CONN.get((ref, str(p)), "")
        ax.text(x + 0.2, yy, f"{p:>2}", fontsize=7, family="monospace", va="center")
        ax.text(x + 0.75, yy, names.get(p, ""), fontsize=7, va="center")
        col = DOM.get(net, "#c53030" if net else "#999")
        if net in ("GND",): col = "#222"
        ax.plot([x + w, x + w + 0.35], [yy, yy], color="#555", lw=0.8)
        ax.text(x + w + 0.45, yy, net if net else "nc", fontsize=7, va="center", color=col,
                weight="bold" if net else "normal")
    return h

def page1(pdf):
    fig, ax = plt.subplots(figsize=(16.5, 11.7)); ax.set_xlim(0, 48); ax.set_ylim(0, 34); ax.axis("off")
    ax.text(0.5, 33.2, "VisorMesh SB-LoRa v0.1  -  Springboard MeshCore companion module", fontsize=14, weight="bold")
    ax.text(0.5, 32.5, "Net-label schematic: pins with the same net name are connected. Blue = Springboard VCC domain (VSB), "
            "amber = XIAO 3V3 domain (3V3X).", fontsize=8.5, color="#444")
    # J1 two columns
    names = {p: SB_NAMES[p] for p in range(1, 69)}
    block(ax, 0.5, 31.5, "J1", "pins 1-34 (top)", list(range(1, 35)), names, w=4.2, desc=False)
    block(ax, 7.2, 31.5, "J1", "pins 35-68 (bottom)", list(range(35, 69)), names, w=4.2, desc=False)
    ax.text(0.5, 31.75, "J1 Springboard 68p straddle: Hirose IC1HA-68RD-1.27SH(51), hand-solder", fontsize=7.5, color="#444")
    x = 15.0
    for ref, title in (("U1", "74LVC1G80GW"), ("U2", "74LVC1G80GW"), ("U3", "74LVC1G04GW")):
        pass
    y = 31.5
    for ref, title in (("U1", "74LVC1G80GW"), ("U2", "74LVC1G80GW"), ("U3", "74LVC1G04GW")):
        h = block(ax, x, y, ref, title, pins_of(ref), PIN[ref], w=4.0); y -= h + 0.6
    block(ax, x, y, "R1", "1k", [1, 2], {1: "", 2: ""}, w=4.0)
    x2 = 22.4
    h = block(ax, x2, 31.5, "U4", "SC16IS740IPW", pins_of("U4"), PIN["U4"], w=4.6)
    y = 31.5 - h - 0.6
    h = block(ax, x2, y, "Y1", "14.7456 MHz", pins_of("Y1"), PIN["Y1"], w=4.6); y -= h + 0.6
    for ref in ("C6", "C7"):
        h = block(ax, x2, y, ref, PARTS[ref][0], [1, 2], {}, w=4.6); y -= h + 0.4
    x3 = 31.0; y = 31.5
    for ref in ("U5", "U6", "U7"):
        h = block(ax, x3, y, ref, "SN74LVC1T45", pins_of(ref), PIN[ref], w=4.4); y -= h + 0.5
    x4 = 39.6; y = 31.5
    xn = {k: XIAO_NAMES[k] for k in range(1, 8)}
    h = block(ax, x4, y, "J2", "XIAO left (pins 1-7)", list(range(1, 8)), xn, w=4.4); y -= h + 0.5
    xr = {k: XIAO_NAMES[15 - k] for k in range(1, 8)}
    h = block(ax, x4, y, "J3", "XIAO right (pins 14-8)", list(range(1, 8)), xr, w=4.4); y -= h + 0.5
    h = block(ax, x4, y, "D1", "B5819W", [1, 2], PIN["D1"], w=4.4); y -= h + 0.4
    h = block(ax, x4, y, "JP1", "solder jumper", [1, 2], PIN["JP1"], w=4.4); y -= h + 0.4
    # passives summary
    rows = []
    for ref in sorted(PARTS, key=lambda r: (r[0], int("".join(c for c in r if c.isdigit()) or 0))):
        if ref[0] in "RC" and ref not in ("C6", "C7", "R1"):
            rows.append(f"{ref:<4} {PARTS[ref][0]:<6} {CONN.get((ref,'1'),''):<14} - {CONN.get((ref,'2'),''):<10} {PARTS[ref][4]}")
    ax.text(14.2, 8.6, "Passives", fontsize=8.5, weight="bold")
    for i, r in enumerate(rows):
        ax.text(14.2, 8.0 - i * 0.42, r, fontsize=6.6, family="monospace")
    pdf.savefig(fig); plt.close(fig)

def page2(pdf):
    fig, ax = plt.subplots(figsize=(16.5, 11.7)); ax.set_xlim(0, 48); ax.set_ylim(0, 34); ax.axis("off")
    ax.text(0.5, 33.2, "Theory of operation", fontsize=14, weight="bold")
    def box(x, y, w, h, t, fc):
        ax.add_patch(Rectangle((x, y), w, h, fc=fc, ec="#333", lw=1.2)); ax.text(x + w / 2, y + h / 2, t, ha="center", va="center", fontsize=9)
    def arrow(x0, y0, x1, y1, t=""):
        ax.annotate("", (x1, y1), (x0, y0), arrowprops=dict(arrowstyle="->", lw=1.2))
        if t: ax.text((x0 + x1) / 2, (y0 + y1) / 2 + 0.35, t, ha="center", fontsize=7.5)
    ax.add_patch(Rectangle((8.5, 18), 18, 12, fc="#ebf4ff", ec="#2b6cb0", lw=1, ls="--"))
    ax.text(9, 29.4, "VSB domain (Springboard VCC, off when Visor removes slot power)", fontsize=8, color="#2b6cb0")
    ax.add_patch(Rectangle((29, 18), 16.5, 12, fc="#fff8e6", ec="#b7791f", lw=1, ls="--"))
    ax.text(29.5, 29.4, "3V3X domain (XIAO + LiPo, always on)", fontsize=8, color="#b7791f")
    box(0.5, 22, 6, 4, "Visor\nSpringboard bus\nCS0* CS1* A17 A8\nD0 IRQ* RESET*", "#eee")
    box(10, 25, 5, 2.4, "U1/U2 latch\nA17->nCS  A8->MOSI\non CS0* rise", "#fff")
    box(10, 21.2, 5, 2.4, "U3 inverter\nCS1* -> SCLK", "#fff")
    box(18.5, 21.5, 6, 5.5, "U4 SC16IS740\nSPI mode 0\n64B FIFOs\n115200 8N1\n(14.7456 MHz)", "#fff")
    box(26.2, 23, 4.4, 3, "U5/U6/U7\nLVC1T45\nIoff both ports", "#fff")
    box(33, 21.5, 6, 5.5, "XIAO nRF52840\nMeshCore companion\n(UART on D6/D7)", "#fff")
    box(41, 22.5, 4, 3.5, "Wio-SX1262\nLoRa", "#fff")
    arrow(6.5, 25.5, 10, 26.2, "CS0*,A17,A8"); arrow(6.5, 23, 10, 22.4, "CS1*")
    arrow(15, 26.2, 18.5, 25.5, "nCS, MOSI"); arrow(15, 22.4, 18.5, 23, "SCLK")
    ax.annotate("", (3.5, 22), (21.5, 21.5), arrowprops=dict(arrowstyle="->", lw=1.2, connectionstyle="angle,angleA=-90,angleB=0,rad=0")) if False else None
    ax.plot([21.5, 21.5, 3.5], [21.5, 19.6, 19.6], color="black", lw=1.2); ax.annotate("", (3.5, 22), (3.5, 19.6), arrowprops=dict(arrowstyle="->", lw=1.2))
    ax.text(12.5, 19.0, "SO -> R1 1k -> D0 (sampled during each CS1* read)", ha="center", fontsize=7.5)
    arrow(24.5, 25, 26.2, 25, "TX/RX"); arrow(30.6, 25, 33, 25, "D7/D6")
    arrow(39, 24.2, 41, 24.2, "SPI")
    notes = [
        "Bus protocol (identical address bits to springSD, so its 68k SPI routines carry over):",
        "  - Any CS0* access latches A17 and A8 on the rising edge of CS0*. U1/U2 are Q-bar outputs, so nCS = NOT A17 and MOSI = NOT A8.",
        "    To assert chip select, touch csSlot0 with A17=1; to send bit b, touch csSlot0 with A8 = NOT b (A17 kept at 1).",
        "  - Any CS1* read produces one SCLK pulse (high while CS1* is low). The SC16IS740 samples SI on the rising edge (SPI mode 0)",
        "    and shifts SO on the falling edge; read bit 0 of the 16-bit word returned by the CS1* access to get MISO.",
        "  - SC16IS740 register byte: bit7 R/W (1=read), bits6:3 register address, bits2:1 channel (00), bit0 unused.",
        "  - SC16IS740 IRQ* is open-drain straight onto Springboard IRQ* (the Visor provides the pull-up). RESET* follows the slot RESET*.",
        "Power-domain rules:",
        "  - Only U5-U7 touch both domains. SN74LVC1T45 ports go high-impedance when either VCC is 0 V, so pulling the module, a Visor",
        "    sleep/LOWBAT power cut, or a flat LiPo never back-powers the other side. R2/R3 keep both UART RX lines idle-high.",
        "  - U7 reports slot power to XIAO D0 (VISOR_PRESENT, high = Visor powering the slot). Firmware can gate UART output on it.",
        "  - Springboard VCC load: U1-U4 + VCCA of U5-U7, a few mA active, under 100 mA. Put the SC16IS740 in sleep (IER[4]) when idle",
        "    to stay inside the 100 uA standby budget.",
        "  - JP1 (default open) routes VDOCK (4.75-6.2 V, dock only) through D1 into the XIAO 5V pin so the XIAO charger can top up the",
        "    LiPo in a charging dock. Do NOT bridge JP1 if you will ever plug USB-C in while docked: VDOCK would back-feed the USB host.",
    ]
    for i, n in enumerate(notes):
        ax.text(0.5, 16.0 - i * 0.62, n, fontsize=8.2, family="monospace" if n.startswith("  ") else None)
    pdf.savefig(fig); plt.close(fig)

with PdfPages(sys.argv[1]) as pdf:
    page1(pdf); page2(pdf)
print("ok")
