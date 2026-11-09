"""
VisorMesh Springboard module, rev 2: parts and connectivity. Draft: not laid out or built.
Same shape as ../source/netlist.py (rev 1, hw-v0.1), so its board and schematic scripts can be adapted.
See ../REV2.md for the reasons behind each choice.

Power domains
  VSB   : Springboard VCC (3.0-3.6 V, 100 mA max), present only while the Visor powers the slot.
  3V3X  : XIAO nRF52840 3V3 output, always on (XIAO runs from its own LiPo).
Every signal that crosses VSB <-> 3V3X goes through an SN74LVC1T45/2T45 (Ioff on both ports),
so neither domain can back-power the other.

Bus (Springboard A[23:0] are byte addresses; every access is 16 bits wide, so A0 is unused)
  csSlot0: U1 S29AL016J NOR flash, 1M x 16. Holds the module image (header + installer + app).
           Flash A0-A19 = slot A1-A20. WE# goes through JP2 (cut to write-protect).
  csSlot1: U2 SC16C650B UART, registers at word addresses (UART A0-A2 = slot A1-A3), data on D0-D7.
           The slot allows OE*/WE* at the same moment as CS1* (Dev Guide table 6-2), but the UART
           wants its strobe 10 ns after CS. So the UART's CS is tied active and U3 makes the strobes:
           IOR = OE* OR CS1*, IOW = WE* OR CS1*.
  U4 inverts the UART's active-high INT and RESET against the slot's IRQ* and RESET*.

Pin tables that still need checking against a datasheet are marked VERIFY.
"""

# ref: (value, footprint_lib, footprint_name, lcsc, description, dnp)
# An empty LCSC number is still to be looked up.
PARTS = {
    "J1": ("Springboard 68p straddle", None, "SPRINGBOARD_68", "",
           "Hirose IC1HA-68RD-1.27SH(51); pending the order (REV2.md). Hand-solder", True),
    "U1": ("S29AL016J70TFI020", "Package_SO", "TSOP-I-48_18.4x12mm_P0.5mm", "C914935",
           "2 MB NOR flash, x16, 70 ns, csSlot0", False),
    "U2": ("SC16C650BIBS", "Package_DFN_QFN", "HVQFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm", "C729132",
           "UART, 32-byte FIFOs, csSlot1 (SC16C750BIBS fits the same footprint)", False),
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

# U1 S29AL016J, 48-pin standard TSOP (datasheet figure 3.1). The -020 suffix is the bottom-boot part:
# IDs 01h/2249h, the same as the Am29LV160BB in Handspring's flash driver table.
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

# U2 SC16C650B HVQFN32 (NXP datasheet table 2; the SC16C750B matches, the SC16C550B does not).
# CS, IOR, IOW are the active-low inputs in this package. The exposed pad is ground.
for pad, net in {1: "D5", 2: "D6", 3: "D7", 4: "UART_BAUD", 5: "UART_RX", 6: "UART_TX", 7: "GND",
                 8: "UART_BAUD", 9: "XTAL1", 10: "XTAL2", 11: "UART_IOW_N", 13: "GND", 14: "UART_IOR_N",
                 16: "A3", 17: "A2", 18: "A1", 20: "UART_INT", 24: "UART_RESET", 25: "GND", 26: "GND",
                 27: "VSB", 28: "D0", 29: "D1", 30: "D2", 31: "D3", 32: "D4", "EP": "GND"}.items():
    c("U2", pad, net)            # 4 RCLK = 8 BAUDOUT; 25 CTS / 26 DSR held asserted; 12 n.c.; 15, 19, 21-23 outputs unused
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
