"""
VisorMesh Springboard module, rev 2: parts and connectivity. Laid out (build_board.py), not built. J1's land pattern is rev 1's, checked against an MCS-068SMA1.
Same shape as ../source/netlist.py (rev 1, hw-v0.1), so its board and schematic scripts can be adapted.
See ../REV2.md for the reasons behind each choice.

Power domains
  VSB   : Springboard VCC (3.0-3.6 V, 100 mA max), present only while the Visor powers the slot.
  3V3X  : XIAO nRF52840 3V3 output, always on (XIAO runs from its own LiPo).
Every signal that crosses VSB <-> 3V3X goes through an SN74LVC1T45/2T45 (Ioff on both ports),
so neither domain can back-power the other.

Bus (Springboard A[23:0] are byte addresses; every access is 16 bits wide, so A0 is unused)
  csSlot0: U1 S29AL008J NOR flash, 512K x 16 (pin 9, slot A20, is unused on this size). Holds the module image (header + installer + app).
           Flash A0-A19 = slot A1-A20. WE# goes through JP2 (cut to write-protect).
  csSlot1: U2 TL16C2550 dual UART, channel A only, registers at word addresses (UART A0-A2 = slot A1-A3), data on D0-D7.
           The slot allows OE*/WE* at the same moment as CS1* (Dev Guide table 6-2), but the UART
           wants its strobe 10 ns after CS. So the UART's CS is tied active and U3 makes the strobes:
           IOR = OE* OR CS1*, IOW = WE* OR CS1*.
  U4 inverts the UART's active-high INT and RESET against the slot's IRQ* and RESET*.

Pin tables that still need checking against a datasheet are marked VERIFY.
"""

# ref: (value, footprint_lib, footprint_name, lcsc, description, dnp)
# An empty LCSC number is still to be looked up.
PARTS = {
    "J1": ("MCS-068SMA1-TR30", None, "SPRINGBOARD_68_STRADDLE", "",
           "Robinson Nugent / 3M PC Card receptacle, card side, straddle mount, on a 0.8 mm board. Push it on with the board's two tabs in its ear holes, then hand-solder both rows", True),
    "U1": ("S29AL008J70TFI010", "Package_SO", "TSOP-I-48_18.4x12mm_P0.5mm", "C2759923",
           "1 MB NOR flash, x16, 70 ns, top boot, csSlot0 (S29AL016J pin-compatible)", False),
    "U2": ("TL16C2550RHBR", "Package_DFN_QFN", "Texas_RHB0032E_VQFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm", "C2653222",
           "Dual UART, channel A used, 16-byte FIFOs, csSlot1", False),
    "U3": ("SN74LVC2G32DCUR", "Package_SO", "VSSOP-8_2.3x2mm_P0.5mm", "C91874", "OR: UART read/write strobes", False),
    "U4": ("SN74LVC2G04DCKR", "Package_TO_SOT_SMD", "SOT-363_SC-70-6", "C105251", "Inverters: INT -> IRQ*, RESET* -> RESET", False),
    "U5": ("SN74LVC2T45DCUR", "Package_SO", "VSSOP-8_2.3x2mm_P0.5mm", "C15741",
           "Ioff xlate: UART TX -> XIAO D7, Visor-present -> XIAO D0", False),
    "U6": ("SN74LVC1T45DCKR", "Package_TO_SOT_SMD", "SOT-363_SC-70-6", "C9382", "Ioff xlate: XIAO D6 -> UART RX", False),
    "Y1": ("14.7456MHz 20pF", "Crystal", "Crystal_SMD_3225-4Pin_3.2x2.5mm", "C2901666", "SX3B14.7456F2010F30", False),
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
    "C12": ("1uF", "Capacitor_SMD", "C_0603_1608Metric", "C15849", "3V3X bulk", False),
    "R1": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "XIAO RX idle-high pull-up", False),
    "R2": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "UART RX idle-high pull-up", False),
    "R3": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "Visor-present pull-down", False),
    "R4": ("10k", "Resistor_SMD", "R_0603_1608Metric", "C25804", "Flash WE# pull-up (write-protect with JP2 cut)", False),
    "R5": ("100k", "Resistor_SMD", "R_0603_1608Metric", "C25803", "UART INT pull-down (INTA floats until MCR bit 3 is set)", False),
    "D1": ("B5819W", "Diode_SMD", "D_SOD-123", "C8598", "VDOCK -> XIAO VBUS (only if JP1 bridged)", False),
    "JP1": ("VDOCK EN", "Jumper", "SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", "", "Open by default", False),
    "JP2": ("FLASH WE", "Jumper", "SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm", "",
            "Bridged by default; cut to write-protect the flash", False),
    "X1": ("XIAO nRF52840", "visormesh", "XIAO_SMD", "",
           "Soldered flat by its castellated pads; bottom pads reached through solder-filled plated holes", True),
    "X2": ("Wio-SX1262 for XIAO", "visormesh", "WIO_PTH", "",
           "Male pins through the carrier, spacer off, cut flush; top socket removed", True),
    "BT1": ("LiPo", "visormesh", "BAT_PADS", "", "Flat pouch LiPo, solder pads (size in REV2.md)", True),
    "TP1": ("SWDIO", "TestPoint", "TestPoint_Pad_D1.0mm", "", "XIAO SWD", True),
    "TP2": ("SWCLK", "TestPoint", "TestPoint_Pad_D1.0mm", "", "XIAO SWD", True),
    "TP3": ("NRST", "TestPoint", "TestPoint_Pad_D1.0mm", "", "XIAO reset", True),
    "TP4": ("GND", "TestPoint", "TestPoint_Pad_D1.0mm", "", "Probe ground", True),
}

# lcsc: (manufacturer, part number, full spec), for assemblers that buy from distributors (PCBWay).
# Read from LCSC's listing of each number, 2026-11-26. The passives' specs are what a substitute must meet.
MFR = {
    "C2759923": ("Infineon (Cypress)", "S29AL008J70TFI010", "8 Mbit NOR flash, x16, 70 ns, top boot, 3.0 V, TSOP-48"),
    "C2653222": ("Texas Instruments", "TL16C2550RHBR", "Dual UART, 16-byte FIFOs, VQFN-32 5x5 mm"),
    "C91874": ("Texas Instruments", "SN74LVC2G32DCUR", "Dual 2-input OR, VSSOP-8"),
    "C105251": ("Texas Instruments", "SN74LVC2G04DCKR", "Dual inverter, SC-70-6"),
    "C15741": ("Texas Instruments", "SN74LVC2T45DCUR", "2-bit level translator, VSSOP-8"),
    "C9382": ("Texas Instruments", "SN74LVC1T45DCKR", "1-bit level translator, SC-70-6"),
    "C2901666": ("SCTF", "SX3B14.7456F2010F30", "Crystal 14.7456 MHz, 20 pF load, +-10 ppm, 3225 4-pad"),
    "C15849": ("Samsung Electro-Mechanics", "CL10A105KB8NNNC", "1 uF 50 V X5R +-10 % 0603"),
    "C14663": ("Yageo", "CC0603KRX7R9BB104", "100 nF 50 V X7R +-10 % 0603"),
    "C24658": ("FH (Fenghua)", "0805CG220J500NT", "22 pF 50 V C0G +-5 % 0805"),
    "C25803": ("Uni-Royal", "0603WAF1003T5E", "100 k +-1 % 0.1 W 0603"),
    "C25804": ("Uni-Royal", "0603WAF1002T5E", "10 k +-1 % 0.1 W 0603"),
    "C8598": ("JSCJ (Changjiang)", "B5819W", "Schottky 40 V 1 A, SOD-123"),
}

SB_NAMES = {  # Table 5-1 of the Springboard Development Guide
    1: "GND", 2: "D3", 3: "D4", 4: "D5", 5: "D6", 6: "D7", 7: "CS0*", 8: "A10", 9: "OE*", 10: "A11", 11: "A9",
    12: "A8", 13: "A13", 14: "A14", 15: "WE*", 16: "IRQ*", 17: "VCC", 18: "VDOCK", 19: "A16", 20: "A15",
    21: "A12", 22: "A7", 23: "A6", 24: "A5", 25: "A4", 26: "A3", 27: "A2", 28: "A1", 29: "A0", 30: "D0",
    31: "D1", 32: "D2", 33: "RSVD", 34: "GND", 35: "GND", 36: "CD1*", 37: "D11", 38: "D12", 39: "D13",
    40: "D14", 41: "D15", 42: "CS1*", 43: "RSVD", 44: "RSVD", 45: "RSVD", 46: "A17", 47: "A18", 48: "A19",
    49: "A20", 50: "A21", 51: "VCC", 52: "VDOCK", 53: "A22", 54: "A23", 55: "RSVD", 56: "RSVD", 57: "RSVD",
    58: "RESET*", 59: "RSVD", 60: "MIC-", 61: "MIC+", 62: "RSVD", 63: "LOWBAT*", 64: "D8", 65: "D9",
    66: "D10", 67: "CD2*", 68: "GND",
}

# Springboard pin -> net. CD1*/CD2* tied to GND per spec; A0, A21-A23, LOWBAT*, MIC and RSVD unused.
SB = {}
for p, name in SB_NAMES.items():
    if name in ("GND", "CD1*", "CD2*"):
        SB[p] = "GND"
    elif name == "VCC":
        SB[p] = "VSB"
    elif name in ("VDOCK",):
        SB[p] = name
    elif name[0] == "D" and name[1:].isdigit():
        SB[p] = name
    elif name[0] == "A" and name[1:].isdigit() and 1 <= int(name[1:]) <= 20:
        SB[p] = name
    elif name in ("CS0*", "CS1*", "OE*", "WE*", "IRQ*", "RESET*"):
        SB[p] = name[:-1] + "_N"

# (ref, pad) -> net
CONN = {}
for p, n in SB.items():
    CONN[("J1", str(p))] = n

def c(ref, pad, net):
    CONN[(ref, str(pad))] = net

# U1 S29AL008J, 48-pin standard TSOP, the same pinout as the S29AL016J it replaced (out of stock) with pin 9
# (A19 there) unused. The -010 suffix is the top-boot part: IDs 01h/22DAh, as the Am29LV800BT.
FLASH_A = {25: 0, 24: 1, 23: 2, 22: 3, 21: 4, 20: 5, 19: 6, 18: 7, 8: 8, 7: 9, 6: 10, 5: 11, 4: 12,
           3: 13, 2: 14, 1: 15, 48: 16, 17: 17, 16: 18, 9: 19}
for pad, bit in FLASH_A.items():
    c("U1", pad, "A%d" % (bit + 1))         # word address: flash A0 = slot A1
FLASH_DQ = {29: 0, 31: 1, 33: 2, 35: 3, 38: 4, 40: 5, 42: 6, 44: 7,
            30: 8, 32: 9, 34: 10, 36: 11, 39: 12, 41: 13, 43: 14, 45: 15}
for pad, bit in FLASH_DQ.items():
    c("U1", pad, "D%d" % bit)
for pad, net in {11: "WE_FL_N", 12: "RESET_N", 26: "CS0_N", 27: "GND", 28: "OE_N",
                 37: "VSB", 46: "GND", 47: "VSB"}.items():
    c("U1", pad, net)            # 47 BYTE# high = word mode; 15 RY/BY# open (status is polled); 14 WP# open (internal pull-up); 10, 13 NC
c("JP2", 1, "WE_N"); c("JP2", 2, "WE_FL_N")
c("R4", 1, "VSB"); c("R4", 2, "WE_FL_N")

# U2 TL16C2550 VQFN-32 RHB (TI SLWS161E pin functions). Channel A only; CSA is tied active and U3 gates the
# strobes, CSB is tied inactive. CTSA/CTSB held asserted, RXB held idle. The exposed pad (33) is ground.
for pad, net in {1: "D6", 2: "D7", 3: "VSB", 4: "UART_RX", 5: "UART_TX", 7: "GND", 8: "VSB",
                 10: "XTAL1", 11: "XTAL2", 12: "UART_IOW_N", 13: "GND", 14: "UART_IOR_N", 16: "GND",
                 18: "A3", 19: "A2", 20: "A1", 21: "UART_INT", 24: "UART_RESET", 25: "GND", 26: "VSB",
                 27: "D0", 28: "D1", 29: "D2", 30: "D3", 31: "D4", 32: "D5", 33: "GND"}.items():
    c("U2", pad, net)            # 6 TXB, 15 RTSB, 22 INTB, 23 RTSA unused outputs; 9, 17 n.c.
c("R5", 1, "UART_INT"); c("R5", 2, "GND")
# Y1 3225 4-pad: 1/3 crystal, 2/4 case
c("Y1", 1, "XTAL1"); c("Y1", 3, "XTAL2"); c("Y1", 2, "GND"); c("Y1", 4, "GND")
c("C6", 1, "XTAL1"); c("C6", 2, "GND"); c("C7", 1, "XTAL2"); c("C7", 2, "GND")
# U3 SN74LVC2G32 (VSSOP-8): 1 1A, 2 1B, 3 2Y, 4 GND, 5 2A, 6 2B, 7 1Y, 8 VCC
for pad, net in {1: "OE_N", 2: "CS1_N", 7: "UART_IOR_N", 5: "WE_N", 6: "CS1_N", 3: "UART_IOW_N",
                 4: "GND", 8: "VSB"}.items():
    c("U3", pad, net)
# U4 SN74LVC2G04 (SC-70-6): 1 1A, 2 GND, 3 2A, 4 2Y, 5 VCC, 6 1Y
for pad, net in {1: "UART_INT", 6: "IRQ_N", 3: "RESET_N", 4: "UART_RESET", 2: "GND", 5: "VSB"}.items():
    c("U4", pad, net)
# U5 SN74LVC2T45 (VSSOP-8): 1 VCCA, 2 A1, 3 A2, 4 GND, 5 DIR, 6 B2, 7 B1, 8 VCCB. DIR high = A->B
for pad, net in {1: "VSB", 2: "UART_TX", 3: "VSB", 4: "GND", 5: "VSB", 6: "VISOR_PRESENT", 7: "XIAO_RX",
                 8: "3V3X"}.items():
    c("U5", pad, net)
# U6 SN74LVC1T45 (SC-70-6): 1 VCCA, 2 GND, 3 A, 4 B, 5 DIR, 6 VCCB. DIR low = B->A
for pad, net in {1: "VSB", 2: "GND", 3: "UART_RX", 4: "XIAO_TX", 5: "GND", 6: "3V3X"}.items():
    c("U6", pad, net)
c("R1", 1, "3V3X"); c("R1", 2, "XIAO_RX")
c("R2", 1, "VSB"); c("R2", 2, "UART_RX")
c("R3", 1, "VISOR_PRESENT"); c("R3", 2, "GND")
# Decoupling
for ref, net in (("C1", "VSB"), ("C2", "VSB"), ("C3", "VSB"), ("C4", "VSB"), ("C5", "VSB"),
                 ("C8", "VSB"), ("C10", "VSB"), ("C9", "3V3X"), ("C11", "3V3X"), ("C12", "3V3X")):
    c(ref, 1, net); c(ref, 2, "GND")
# VDOCK charging path (optional)
c("D1", 2, "VDOCK"); c("D1", 1, "VDOCK_D")      # SOD-123 pad 1 = cathode
c("JP1", 1, "VDOCK_D"); c("JP1", 2, "XIAO_VBUS")

# X1 XIAO nRF52840 edge pads (top view, USB-C toward the outer edge) and bottom pads.
# Radio pins from MeshCore's xiao_nrf52 variant; D6/D7 is the companion UART (firmware/ patch).
XIAO_NAMES = {1: "D0", 2: "D1", 3: "D2", 4: "D3", 5: "D4", 6: "D5", 7: "D6", 8: "D7", 9: "D8",
              10: "D9", 11: "D10", 12: "3V3", 13: "GND", 14: "5V"}
XIAO = {1: "VISOR_PRESENT", 2: "LORA_DIO1", 3: "LORA_RESET", 4: "LORA_BUSY", 5: "LORA_NSS",
        6: "LORA_RXEN", 7: "XIAO_TX", 8: "XIAO_RX", 9: "LORA_SCK", 10: "LORA_MISO", 11: "LORA_MOSI",
        12: "3V3X", 13: "GND", 14: "XIAO_VBUS"}
for k, n in XIAO.items():
    c("X1", k, n)
# Bottom pads (visormesh.pretty/XIAO_SMD, positions from Seeed's XIAO nRF52840 v1.3 board file).
for pad, net in {"BAT+": "VBAT", "BAT-": "GND", "SWDIO": "SWDIO", "SWCLK": "SWCLK", "RST": "NRST",
                 "GND": "GND"}.items():
    c("X1", pad, net)
c("BT1", 1, "VBAT"); c("BT1", 2, "GND")
c("TP1", 1, "SWDIO"); c("TP2", 1, "SWCLK"); c("TP3", 1, "NRST"); c("TP4", 1, "GND")

# X2 Wio-SX1262: pin k is the pin that took XIAO pin k when stacked. Only the radio nets join;
# its D0, D6, D7 and 5V pins stay unconnected so the UART and Visor-present lines see only the XIAO.
for k in (2, 3, 4, 5, 6, 9, 10, 11, 12, 13):
    c("X2", k, XIAO[k])

NETS = sorted(set(CONN.values()))
