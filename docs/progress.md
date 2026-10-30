# Progress

## Milestones

### Hardware: SB-LoRa v0.1
- [x] Board designed, DRC clean, fab package generated
- [ ] Check part rotations in JLC preview (U1–U7, Y1, D1)
- [ ] Order PCBs + assembly (2-layer, 1.6 mm, ENIG)
- [ ] Order J1 (Hirose IC1HA-68RD-1.27SH(51), DigiKey HR1187-ND) and 2x 1x7 female headers
- [ ] Bare board: continuity check, dry-fit in Visor slot
- [ ] Fit J1, bench-power VSB at 3.3 V, check current and 14.7456 MHz on Y1
- [ ] In Visor: read/write SC16IS740 scratch register (SPR, 0x07) from Palm side

### Firmware
- [ ] Apply `firmware/meshcore-xiao-nrf52-uart-companion.patch` to MeshCore, build `Xiao_nrf52_companion_radio_uart`
- [ ] Bench-test over 3.3 V USB-TTL on D6/D7 with meshcore-cli
- [ ] Plug stack into carrier; `CMD_DEVICE_QUERY` gets a DEVICE_INFO reply

### Palm client
- [ ] Toolchain set up (prc-tools or CodeWarrior, Palm OS 3.5 SDK, HsExt headers)
- [ ] POSE running a 3.5.2H3 Visor ROM
- [ ] Port springSD bus routines; SC16IS740 driver
- [ ] Companion protocol framing + minimal command set
- [ ] UI: contacts, DMs, channels
- [ ] Install method confirmed (HotSync or CF adapter)

## Open questions
- Which CF adapter is it, and does its ROM include an app installer?
- Is the J1 footprint correct against the Hirose drawing? (Only checked against springSD.)
- Does the extension clear the Visor body? Is the slot-opening alignment right?

## Log

### 2026-10-30
- Planning done in claude.ai. Chose a custom Springboard module (SB-LoRa) over cradle serial/IR.
- SB-LoRa v0.1 fab package generated. Repo set up and files organized.
