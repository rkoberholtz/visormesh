# VisorMesh SB-LoRa v0.1

A Springboard module for the Handspring Visor that carries a Seeed XIAO nRF52840 + Wio-SX1262 stack running MeshCore companion firmware. The Visor talks to it over a bit-banged SPI link into an SC16IS740 UART bridge. The XIAO runs from its own LiPo and keeps meshing while the Visor sleeps or the module is unplugged.

**Status: prototype design, not yet built.** DRC is clean (0 unconnected pads) and the netlist was cross-checked against the Springboard Development Guide pin table, the SC16IS740 datasheet pinout, and Dmitry Grinberg's springSD v1.1 connector footprint. Order 5 boards and build one before trusting it.

## What's in here

| Path | What |
|---|---|
| `fab/visormesh_gerbers_jlcpcb.zip` | Gerbers + Excellon drill, upload as-is to JLCPCB or PCBWay |
| `fab/visormesh_bom_jlcpcb.csv` | BOM, JLC format (LCSC part numbers) |
| `fab/visormesh_cpl_jlcpcb.csv` | Pick-and-place, JLC format, top side only |
| `fab/hand_fit_parts.csv` | Parts you fit yourself (J1, J2, J3, JP1) |
| `fab/visormesh_schematic.pdf` | Net-label schematic + theory of operation |
| `fab/visormesh_assembly_top.pdf` | Assembly drawing with reference designators |
| `kicad/` | KiCad 7 board + project + custom DRC rule (opens in KiCad 7 or 8) |
| `source/` | The scripts that generated everything. `netlist.py` is the single source of truth |
| `../../firmware/` | MeshCore patch: `Xiao_nrf52_companion_radio_uart` env (companion protocol on D6/D7) |
| `renders/` | Top and bottom copper renders |

## Ordering (JLCPCB)

- PCB: 2 layers, **1.6 mm**, 1 oz, any color. **ENIG recommended** (flat pads for the 0.65 mm TSSOP, SC-70 parts and the straddle connector); lead-free HASL works too.
- Board is 50.43 x 75.15 mm. The bottom 50.47 mm is the in-slot section, using springSD's exact outline. The top 24.6 mm sticks out of the Visor and carries the XIAO stack.
- Assembly: top side, Economic PCBA is fine. Upload BOM + CPL. **Check part rotations in JLC's placement preview** before paying, especially U1-U7, Y1 and D1; KiCad and JLC disagree on rotation for some SOT/SC-70 footprints. Pin 1 of each IC is marked on the silkscreen and assembly drawing. The CPL already includes the corrections found in the first order's preview (U1, U2, U4 at 270; U3, U5-U7 at 180), so the dots should land on the pin-1 triangles without adjustment. Still glance at the preview: JLC can change its part models.
- Extended parts: SC16IS740 (C407851), 74LVC1G80 (C353857), crystal (C2901666) and probably the SN74LVC1T45 (C9382) and 74LVC1G04 (C10237). Expect extended-part loading fees.

## Parts you fit by hand

1. **J1 Springboard connector**: Hirose **IC1HA-68RD-1.27SH(51)** (DigiKey HR1187-ND), the PC-Card-style straddle receptacle springSD uses. It clamps the board edge; pins 1-34 land on the top pads, 35-68 on the bottom. Dmitry also sourced keyless 2x34 1.27 mm parts from LCSC (XKB X1321FV-2x34, C2881777); check that part's drawing before substituting. Pin 1 is marked on silk at the right-hand end of the top row.
2. **J2/J3**: two 1x7 2.54 mm female headers (low-profile if you can find them). Your XIAO+Wio stack plugs in here. You can also solder the stack's pins straight into the holes.
3. **JP1**: leave **open**. See VDOCK below.

## Mechanical / orientation

- The XIAO sits with **components facing up (same side as U1-U7) and USB-C at the outer edge**. Silkscreen labels (D0..D6, 5V, GND, 3V3, D10..D7) show which header pin goes where. **Check them against your stack before plugging in.** If your stack only exposes pins upside-down, the labels tell you it's mirrored.
- Solder your LiPo to the XIAO's BAT+/BAT- pads first. The carrier doesn't touch the battery; the XIAO's own charger handles it over USB-C.
- In-slot components are all 1.25 mm or shorter, on the top side only, which is lower than springSD's microSD socket. The bottom side inside the slot is a flat GND pour.
- **Assumption to verify:** like springSD, the top edge of the 50.47 mm section lines up with the slot opening, and the extension (with the stack and antenna) protrudes like a battery-module bump. Dry-fit a bare board before soldering anything.

## Electrical summary

- **Bus SPI** (same wiring as springSD, so its 68k routines port directly):
  - A CS0* access latches A17 and A8 on its rising edge. Through Q-bar, `nCS = NOT A17` and `MOSI = NOT A8`.
  - A CS1* read produces one SCLK pulse; MISO comes back on D0 through a 1k resistor.
  - SC16IS740 is SPI mode 0. Register byte: bit7 = R/W (1 = read), bits 6:3 = register, bits 2:1 = channel 00.
- **UART**: 14.7456 MHz crystal (SCTF SX3B14.7456F2010F30, 20 pF load; replaced the out-of-stock SXT32413BA07 rated 13 pF), so 115200 baud with DLL=8, DLH=0 (prescaler 1). 64-byte TX/RX FIFOs. IRQ* goes straight to the Springboard IRQ* (the Visor has the pull-up). RESET* follows the slot RESET*.
- **Power isolation**: only U5-U7 (SN74LVC1T45) touch both domains, and both of their ports go Hi-Z when either supply is 0 V. Removing the module, a LOWBAT power cut, or a flat LiPo can't back-power the other side.
- **Visor-present**: XIAO D0 is high when the slot is powered. The firmware can stop pushing frames when it's low; the companion queues messages anyway.
- **Slot budget**: a few mA active, against the 100 mA limit. To stay inside the 100 uA standby limit, put the SC16IS740 to sleep (IER bit 4) when the Palm app is idle.
- **VDOCK (JP1)**: when bridged, VDOCK (4.75-6.2 V, only present in a charging dock) feeds the XIAO 5V pin through a Schottky so the XIAO charger can top up the LiPo while docked. The standard Visor USB cradle almost certainly doesn't supply VDOCK on an AAA Visor. **If you bridge JP1, never plug USB-C in while docked**, because VDOCK would back-feed the USB host.

## Bring-up order

1. Bare board, before soldering anything: check continuity of VSB (J1 pins 17/51) and GND, then dry-fit in the Visor slot.
2. After JLC assembly, fit J1 only. Bench-power VSB at 3.3 V through a current-limited supply. Expect a few mA, and the crystal at 14.7456 MHz on Y1.
3. Insert into the Visor with nothing on J2/J3. Port springSD's bus routines and read the SC16IS740 scratch register (SPR, reg 0x07): write and read back a pattern. That proves the CS0/CS1 latch path.
4. Flash the XIAO with `Xiao_nrf52_companion_radio_uart` (patch in `../../firmware/`). Bench-test it over a 3.3 V USB-TTL adapter on D6/D7 with meshcore-cli before plugging the stack into the carrier.
5. Plug in the stack. Configure the SC16IS740 for 115200 8N1, send CMD_DEVICE_QUERY (`<` + len + `0x16 0x03`), and look for a `>`-framed DEVICE_INFO reply.

## Known limits / things I could not verify from here

- Mechanical fit of the extension against the Visor body and the exact slot depth (see the assumption above).
- The J1 footprint is copied pad-for-pad from springSD v1.1 (verified against its IPC-356 netlist). I could not check it against the Hirose drawing.
- JLC part rotations (see Ordering).
- There is no `.kicad_sch`: the schematic is generated from `source/netlist.py` as a PDF, and the board's nets come from the same file. To regenerate on a machine with KiCad 7, Java and Freerouting 2.1: `source/make.sh` (autoroutes), then `make_fab.py`, `verify.py`, `draw_schematic.py`. Routing is nondeterministic between runs.
