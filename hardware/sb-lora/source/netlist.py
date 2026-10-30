"""
VisorMesh Springboard module: single source of truth for parts and connectivity.
Used by build_board.py (PCB), make_bom.py (JLC BOM/CPL) and draw_schematic.py (PDF schematic).

Power domains
  VSB   : Springboard VCC (3.0-3.6 V, 100 mA max), present only while the Visor powers the slot.
  3V3X  : XIAO nRF52840 3V3 output, always on (XIAO runs from its own LiPo).
Every signal that crosses VSB <-> 3V3X goes through an SN74LVC1T45 (Ioff on both ports),
so neither domain can back-power the other.

Bus SPI (copied from Dmitry Grinberg's springSD, same address bits so its driver logic carries over):
  CS0* access latches A17 -> nCS and A8 -> MOSI (74LVC1G80, Q-bar output, rising edge of CS0*)
  CS1* access produces one SCLK pulse (74LVC1G04 inverts CS1*); MISO is read on D0 through 1k.
"""

# ref: (value, footprint_lib, footprint_name, lcsc, description, dnp)
PARTS = {
    "J1": ("Springboard 68p straddle", None, "SPRINGBOARD_68", "",
           "Hirose IC1HA-68RD-1.27SH(51) (DigiKey HR1187-ND); hand-solder", True),
    "U1": ("74LVC1G80GW", "Package_TO_SOT_SMD", "SOT-353_SC-70-5", "C353857", "D-FF, Q-bar: A17 -> nCS", False),
    "U2": ("74LVC1G80GW", "Package_TO_SOT_SMD", "SOT-353_SC-70-5", "C353857", "D-FF, Q-bar: A8 -> MOSI", False),
    "U3": ("74LVC1G04GW", "Package_TO_SOT_SMD", "SOT-353_SC-70-5", "C10237", "Inverter: CS1* -> SCLK", False),
    "U4": ("SC16IS740IPW", "Package_SO", "TSSOP-16_4.4x5mm_P0.65mm", "C407851", "SPI-to-UART bridge, 64B FIFOs", False),
    "U5": ("SN74LVC1T45DCKR", "Package_TO_SOT_SMD", "SOT-363_SC-70-6", "C9382", "Ioff xlate: bridge TX -> XIAO D7", False),
    "U6": ("SN74LVC1T45DCKR", "Package_TO_SOT_SMD", "SOT-363_SC-70-6", "C9382", "Ioff xlate: XIAO D6 -> bridge RX", False),
    "U7": ("SN74LVC1T45DCKR", "Package_TO_SOT_SMD", "SOT-363_SC-70-6", "C9382", "Ioff xlate: Visor-present -> XIAO D0", False),
    "Y1": ("14.7456MHz 13pF", "Crystal", "Crystal_SMD_3225-4Pin_3.2x2.5mm", "C6264361", "SXT32413BA07-14.7456M", False),
    "C1": ("1uF", "Capacitor_SMD", "C_0603_1608Metric", "C15849", "VSB bulk", False),
    "C2": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U1 decoupling", False),
    "C3": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U2 decoupling", False),
    "C4": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U3 decoupling", False),
    "C5": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U4 decoupling", False),
    "C6": ("22pF C0G", "Capacitor_SMD", "C_0805_2012Metric", "C24658", "Y1 load (FH 0805CG220J500NT)", False),
    "C7": ("22pF C0G", "Capacitor_SMD", "C_0805_2012Metric", "C24658", "Y1 load (FH 0805CG220J500NT)", False),
    "C8": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U5 VCCA", False),
    "C9": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U5 VCCB", False),
    "C10": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U6 VCCA", False),
    "C11": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U6 VCCB", False),
    "C12": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U7 VCCA", False),
    "C13": ("100nF", "Capacitor_SMD", "C_0603_1608Metric", "C14663", "U7 VCCB", False),
    "C14": ("1uF", "Capacitor_SMD", "C_0603_1608Metric", "C15849", "3V3X bulk", False),
    "R1": ("1k", "Resistor_SMD", "R_0603_1608Metric", "C21190", "MISO series to D0 (bus contention limit)", False),
    "R2": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "XIAO RX idle-high pull-up", False),
    "R3": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "Bridge RX idle-high pull-up", False),
    "R4": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "Visor-present pull-down", False),
    "D1": ("B5819W", "Diode_SMD", "D_SOD-123", "C8598", "VDOCK -> XIAO VBUS (only if JP1 bridged)", False),
    "JP1": ("VDOCK EN", "Jumper", "SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", "", "Open by default", False),
    "J2": ("XIAO left 1x7", "Connector_PinSocket_2.54mm", "PinSocket_1x07_P2.54mm_Vertical", "",
           "XIAO pins 1-7 (D0..D6); female socket or solder pins direct", True),
    "J3": ("XIAO right 1x7", "Connector_PinSocket_2.54mm", "PinSocket_1x07_P2.54mm_Vertical", "",
           "XIAO pins 14-8 (VBUS,GND,3V3,D10,D9,D8,D7)", True),
}

# Springboard pin -> net (Table 5-1 of the Springboard Development Guide; CD1*/CD2* tied to GND per spec)
SB = {
    1: "GND", 34: "GND", 35: "GND", 68: "GND", 36: "GND", 67: "GND",
    17: "VSB", 51: "VSB",
    18: "VDOCK", 52: "VDOCK",
    7: "CS0_N", 42: "CS1_N", 46: "A17", 12: "A8",
    30: "D0", 16: "IRQ_N", 58: "RESET_N",
}
SB_NAMES = {  # for silkscreen/schematic only
    1: "GND", 2: "D3", 3: "D4", 4: "D5", 5: "D6", 6: "D7", 7: "CS0*", 8: "A10", 9: "OE*", 10: "A11", 11: "A9",
    12: "A8", 13: "A13", 14: "A14", 15: "WE*", 16: "IRQ*", 17: "VCC", 18: "VDOCK", 19: "A16", 20: "A15",
    21: "A12", 22: "A7", 23: "A6", 24: "A5", 25: "A4", 26: "A3", 27: "A2", 28: "A1", 29: "A0", 30: "D0",
    31: "D1", 32: "D2", 33: "RSVD", 34: "GND", 35: "GND", 36: "CD1*", 37: "D11", 38: "D12", 39: "D13",
    40: "D14", 41: "D15", 42: "CS1*", 43: "RSVD", 44: "RSVD", 45: "RSVD", 46: "A17", 47: "A18", 48: "A19",
    49: "A20", 50: "A21", 51: "VCC", 52: "VDOCK", 53: "A22", 54: "A23", 55: "RSVD", 56: "RSVD", 57: "RSVD",
    58: "RESET*", 59: "RSVD", 60: "MIC-", 61: "MIC+", 62: "RSVD", 63: "LOWBAT*", 64: "D8", 65: "D9",
    66: "D10", 67: "CD2*", 68: "GND",
}

# (ref, pad) -> net
CONN = {}
for p, n in SB.items():
    CONN[("J1", str(p))] = n

def c(ref, pad, net):
    CONN[(ref, str(pad))] = net

# U1/U2 74LVC1G80 (SOT-353): 1 D, 2 CP, 3 GND, 4 Q-bar, 5 VCC
for ref, d, q in (("U1", "A17", "SPI_NCS"), ("U2", "A8", "SPI_MOSI")):
    c(ref, 1, d); c(ref, 2, "CS0_N"); c(ref, 3, "GND"); c(ref, 4, q); c(ref, 5, "VSB")
# U3 74LVC1G04 (SOT-353): 1 NC, 2 A, 3 GND, 4 Y, 5 VCC
c("U3", 2, "CS1_N"); c("U3", 3, "GND"); c("U3", 4, "SPI_SCLK"); c("U3", 5, "VSB")
# U4 SC16IS740 TSSOP-16
for pad, net in {1: "VSB", 2: "SPI_NCS", 3: "SPI_MOSI", 4: "SPI_MISO", 5: "SPI_SCLK", 6: "GND", 7: "IRQ_N",
                 8: "GND", 9: "GND", 11: "GND", 12: "BR_TX", 13: "BR_RX", 14: "RESET_N",
                 15: "XTAL1", 16: "XTAL2"}.items():
    c("U4", pad, net)            # pin 10 RTS left open
# Y1 3225 4-pad: 1/3 crystal, 2/4 case
c("Y1", 1, "XTAL1"); c("Y1", 3, "XTAL2"); c("Y1", 2, "GND"); c("Y1", 4, "GND")
c("C6", 1, "XTAL1"); c("C6", 2, "GND"); c("C7", 1, "XTAL2"); c("C7", 2, "GND")
c("R1", 1, "D0"); c("R1", 2, "SPI_MISO")
# SN74LVC1T45 (SC-70-6): 1 VCCA, 2 GND, 3 A, 4 B, 5 DIR, 6 VCCB.  DIR high = A->B
for ref, a, b, d in (("U5", "BR_TX", "XIAO_RX", "VSB"),     # bridge TX -> XIAO D7
                     ("U6", "BR_RX", "XIAO_TX", "GND"),     # XIAO D6 -> bridge RX
                     ("U7", "VSB", "VISOR_PRESENT", "VSB")):  # VSB present -> XIAO D0
    c(ref, 1, "VSB"); c(ref, 2, "GND"); c(ref, 3, a); c(ref, 4, b); c(ref, 5, d); c(ref, 6, "3V3X")
c("R2", 1, "3V3X"); c("R2", 2, "XIAO_RX")
c("R3", 1, "VSB"); c("R3", 2, "BR_RX")
c("R4", 1, "VISOR_PRESENT"); c("R4", 2, "GND")
# Decoupling
for ref, net in (("C1", "VSB"), ("C2", "VSB"), ("C3", "VSB"), ("C4", "VSB"), ("C5", "VSB"),
                 ("C8", "VSB"), ("C10", "VSB"), ("C12", "VSB"),
                 ("C9", "3V3X"), ("C11", "3V3X"), ("C13", "3V3X"), ("C14", "3V3X")):
    c(ref, 1, net); c(ref, 2, "GND")
# VDOCK charging path (optional)
c("D1", 2, "VDOCK"); c("D1", 1, "VDOCK_D")      # SOD-123 pad 1 = cathode
c("JP1", 1, "VDOCK_D"); c("JP1", 2, "XIAO_VBUS")
# XIAO nRF52840 header (top view, USB-C toward outer edge)
XIAO = {1: "VISOR_PRESENT", 2: None, 3: None, 4: None, 5: None, 6: None, 7: "XIAO_TX",
        8: "XIAO_RX", 9: None, 10: None, 11: None, 12: "3V3X", 13: "GND", 14: "XIAO_VBUS"}
XIAO_NAMES = {1: "D0", 2: "D1", 3: "D2", 4: "D3", 5: "D4", 6: "D5", 7: "D6", 8: "D7", 9: "D8",
              10: "D9", 11: "D10", 12: "3V3", 13: "GND", 14: "5V"}
for k in range(1, 8):
    if XIAO[k]: c("J2", k, XIAO[k])
for k in range(1, 8):           # J3 pin k = XIAO pin 15-k
    n = XIAO[15 - k]
    if n: c("J3", k, n)

NETS = sorted(set(CONN.values()))
