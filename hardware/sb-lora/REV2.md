# SB-LoRa rev 2 plan

Draft, nothing designed yet. Rev 1 (`hw-v0.1`) is ordered and shipping, but its Hirose IC1HA straddle connector is obsolete and the order for one may not ship. Rev 2 changes the connector, adds a flash chip so the app loads itself, and makes the module as flat as possible.

## Decided

### Radio: XIAO and Wio side by side, no stack

The XIAO nRF52840 and the Wio-SX1262 board keep their roles but sit flat on the carrier next to each other, instead of stacked on 2.54 mm headers. The stack is the tallest thing on rev 1. Side by side, the height is set by the XIAO's USB-C socket (about 3.2 mm) and the Wio's u.FL.

- Both are 14-pin boards the size of the XIAO. The kit ships them soldered directly on top of each other, so going side by side means desoldering the pair. The rev 1 extension is 50.4 mm wide and 24.6 mm deep, which holds two of them (about 18 x 21 mm each) with the XIAO's USB-C at the outer edge.
- The XIAO is soldered by its castellated edge pads. That covers its bottom pads, so the carrier brings them out:
  - **BAT+ / BAT-** to pads for a flat pouch LiPo (the XIAO's charger still does the charging over USB-C).
  - **SWDIO / SWCLK / RST** to test pads, so the radio can be reflashed with a probe if the bootloader is lost.
- The Wio board's mounting depends on what its edge offers (to measure): castellated pads, or through-holes with pins cut flush.
- The carrier wires the radio nets pin to pin, from the MeshCore `xiao_nrf52` variant:

  | XIAO pin | Net | Goes to |
  |---|---|---|
  | D1 | LORA_DIO1 | Wio |
  | D2 | LORA_RESET | Wio |
  | D3 | LORA_BUSY | Wio |
  | D4 | LORA_NSS | Wio |
  | D5 | SX126X_RXEN | Wio |
  | D8 / D9 / D10 | SCK / MISO / MOSI | Wio |
  | D6 / D7 | UART TX / RX | Visor bridge (as rev 1), not the Wio |
  | 3V3, GND | power | Wio and the XIAO-side level translators |
  | D0 | free | |

- The antenna is the Wio's existing flexible stick-on antenna on its u.FL. It goes inside the module case.

### Memory chip so the app loads itself

A 16-bit-wide 3.3 V NOR flash at the start of csSlot0 (CS0*), as the Springboard Development Guide requires for the module to be recognised (§2.1). With a Palm-MakeROM header and the app on it, the Visor mounts the module on insertion and switches to the app (§3.3; on Palm OS 3.5 and later the Welcome app is chosen by a ROM token, so the database can stay "VisorMesh").

- Candidate: SST39VF1601C (2 MB, x16, 70 ns, TSOP-48, 1.2 mm). Room for the app and the Map basemap.
- WE* is wired so the app can rewrite the flash itself: HotSync a new .prc to RAM, then update the module from the app.
- A write-protect jumper.
- Software: an app running from the module is read-only, so it must not write to its own resource database. To check before rev 2.

This takes CS0*, so the radio link moves to csSlot1 (CS1*). The flash needs about 20 address lines and all 16 data lines, so rev 2 is expected to be a 4-layer board.


## Open

- **Connector: waiting on the Hirose IC1HA order.** If it ships, rev 2 keeps rev 1's straddle footprint. If it's cancelled, switch to a single-sided SMT PC Card receptacle (both rows of tails on the top side), harvested or stocked (TE 5535657-2 and Amphenol 1C1K are candidates; check they're the card-side receptacle). The Springboard uses the same 68-pin PCMCIA contacts (Dev Guide §5, §7.1), and the keying is in the module's plastic case, not the connector. Needed from a sample: markings, tail pitch, row spacing, tail distance from the mating face, and board surface height relative to the contact centre line.
- **Case.** The keying is in the module case, so rev 2 needs a case (printed, or a harvested Springboard shell) whose key and latch match the slot, with the board held at the connector's height. Dry-fit a bare harvested connector in the slot to check the contacts seat fully.
- **Board thickness.** The straddle connector forces 1.6 mm; a top-mount one may allow 0.8-1.0 mm.
- **Link to the radio on CS1*.** Keep rev 1's SC16IS740 SPI bridge (decoded on CS1* with an address line instead of CS0*/CS1*), or use a memory-mapped 16550-style UART (e.g. NXP SC16C750B, 64-byte FIFO) on the bus that the flash already needs. The UART is one bus access per register instead of several per SPI bit, and drops U1-U3; it means rewriting `transport_sb.c` and its simulation.
- **Wio board edge** (castellated pads or only through-holes), for the footprint.
- **Fallback if separating the pair is too risky:** keep the stack as it is and solder it straight to the carrier with no female headers. Thicker than side by side, but thinner than rev 1's stack on headers.
- **LiPo size** for the bump, about 3-4 mm thick.

## Carried over from rev 1

- VDOCK charging option (D1, JP1).
- Level translators between the Visor's slot power and the XIAO's always-on 3V3, so neither side back-powers the other. Merge the three SN74LVC1T45 into a 74LVC2T45 plus one.
- Add test pads on the bus signals and the UART.

## What rev 1 can still tell us

Even without a connector, a bare rev 1 board can be dry-fitted in the slot to check the outline and the assumption that the top of the 50.47 mm section lines up with the slot opening. If the Hirose part does arrive, rev 1 also proves the CS0*/CS1* bus path and the SC16IS740 driver.
