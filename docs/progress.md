# Progress

## Milestones

### Hardware: SB-LoRa v0.1
- [x] Board designed, DRC clean, fab package generated
- [x] Check part rotations in JLC preview (U1–U7, Y1, D1); corrections saved to the CPL
- [x] Order PCBs + assembly from JLCPCB (5 boards, 2-layer, 1.6 mm, leaded HASL); tagged `hw-v0.1`
- [ ] Boards received
- [x] Order J1: Hirose IC1HA-68RD-1.27SH(51) is obsolete; 1 ordered as new-old stock from eBay ($24)
- [ ] Order 2x 1x7 female headers
- [ ] Buy 2–3 cheap used Springboard modules (spare connectors to harvest + slot-fit reference)
- [ ] Bare board: continuity check, dry-fit in Visor slot
- [ ] Fit J1, bench-power VSB at 3.3 V, check current and 14.7456 MHz on Y1
- [ ] In Visor: read/write SC16IS740 scratch register (SPR, 0x07) from Palm side

### Firmware
- [ ] Apply `firmware/meshcore-xiao-nrf52-uart-companion.patch` to MeshCore, build `Xiao_nrf52_companion_radio_uart`
- [ ] Bench-test over 3.3 V USB-TTL on D6/D7 with meshcore-cli
- [ ] Plug stack into carrier; `CMD_DEVICE_QUERY` gets a DEVICE_INFO reply

### Palm client
- [x] Toolchain set up: prc-tools-remix 2.3.6 + PilRC + Palm OS 3.5 SDK in Docker (`palm-client/Dockerfile`)
- [x] Scaffold: builds `visormesh.prc` (main form + "Query radio" → CMD_DEVICE_QUERY); protocol framing with host tests
- [ ] Sync host (Mac mini, Debian 13): visor driver present ✓, build pilot-link into ~/.local, `make install` tested
- [ ] Emulator: CloudpilotEmu with a Palm OS 3.5 ROM (the Visor isn't emulated); optionally POSE + a Visor Neo ROM in a Windows VM
- [ ] Original Handspring SDK headers (Springboard `HsCard*` APIs; the PalmOne `HsExt.h` lacks them), needed for module-ROM work only
- [ ] Port springSD bus routines; SC16IS740 driver
- [ ] Companion protocol framing + minimal command set
- [ ] UI: contacts, DMs, channels
- [ ] Install method confirmed (HotSync or CF adapter)

## Open questions
- Which CF adapter is it, and does its ROM include an app installer?
- Is the J1 footprint correct against the Hirose drawing? (Only checked against springSD.)
- Which Handspring offset variant (−0.3 / 0 / +0.3 / +0.6 mm) is the (51)? This matters if a harvested connector replaces it.
- Long-term J1 supply: no current-production part known. Could a PCMCIA card-side straddle receptacle work? (The keying fit is untested.)
- Does the extension clear the Visor body? Is the slot-opening alignment right?

## Log

### 2026-10-30
- Planning done in claude.ai. Chose a custom Springboard module (SB-LoRa) over cradle serial/IR.
- SB-LoRa v0.1 fab package generated. Repo set up and files organized.
- Crystal Y1 swapped to SX3B14.7456F2010F30 (C2901666, 20 pF load) because C6264361 was out of stock.
- JLC placement preview showed U1, U2, U4 a quarter-turn off and U3, U5–U7 a half-turn off. Fixed in the preview and in the CPL (270 / 180).
- Chose leaded HASL over ENIG (+$18). JLC does the fine-pitch assembly, so pad flatness isn't critical.
- PCB + assembly order placed with JLCPCB. `hw-v0.1` tag moved to the ordered commit.

### 2026-11-01
- J1 (IC1HA-68RD-1.27SH(51)) found to be obsolete at DigiKey and Mouser. Ordered 1 NOS from eBay at $24.
- Removed the unverified XKB/LCSC substitute from the hardware README (it's a vertical SMD socket, not a straddle).
- Starting Palm client development.
