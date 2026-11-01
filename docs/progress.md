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
- [x] Apply `firmware/meshcore-xiao-nrf52-uart-companion.patch` to MeshCore, build `Xiao_nrf52_companion_radio_uart` (`firmware/Makefile`, MeshCore a366955c, GCC 14 on Apple Silicon)
- [x] Flashed to the XIAO; answers `CMD_DEVICE_QUERY` over USB with DEVICE_INFO (v1.17.1)
- [x] Bench-test the D6/D7 UART: FT232R at 3.3 V, 115200 8N1, DEVICE_INFO identical to the USB reply
- [ ] Plug stack into carrier; `CMD_DEVICE_QUERY` gets a DEVICE_INFO reply

### Palm client
- [x] Toolchain set up: prc-tools-remix 2.3.6 + PilRC + Palm OS 3.5 SDK in Docker (`palm-client/Dockerfile`)
- [x] Scaffold: builds `visormesh.prc` (main form + "Query radio" → CMD_DEVICE_QUERY); protocol framing with host tests
- [x] USB link from the dev Mac: pilot-link (desrod @6d696653 + `tools/pilot-link-darwin-usb-open-retry.patch`) lists the Visor's databases
- [x] `make install` puts VisorMesh on the Visor; app launches and shows the main form on the Visor Neo
- [ ] Emulator: CloudpilotEmu with a Palm OS 3.5 ROM (the Visor isn't emulated); optionally POSE + a Visor Neo ROM in a Windows VM
- [ ] Original Handspring SDK headers (Springboard `HsCard*` APIs; the PalmOne `HsExt.h` lacks them), needed for module-ROM work only
- [ ] Port springSD bus routines; SC16IS740 driver
- [x] Companion protocol: framing, builders (DEVICE_QUERY, APP_START, GET_CONTACTS, SYNC_NEXT_MESSAGE), parsers (SELF_INFO, contacts, v3 contact/channel messages); host tests from real captures + MeshCore layouts
- [x] USB bench link: app (`HsUd`) → Visor USB cradle → `tools/visor_bridge.py` → FTDI → XIAO D6/D7. Query radio shows `Reply 0x000D, 82 bytes` on the Visor
- [x] Connect screen: DEVICE_QUERY + APP_START via session.c; shows node, freq, BW/SF/CR, TX power, firmware (verified on the Neo over the USB bridge)
- [x] Node Settings screen: name, preset/radio params, TX power, path hash. Verified on the Neo: SET_RADIO_PARAMS + SET_PATH_HASH_MODE both got RESP_OK and a read-back over the XIAO's USB matches
- [x] Contact list: GET_CONTACTS into a Palm database + contacts screen
- [x] Messages: store, sync on PUSH_MSG_WAITING, send with ack tracking (built; to verify on the Visor)
- [x] MeshTerm-style UI: small 4x6 font, Home (conversations), Conversation, Contacts, Node (built; to verify)
- [ ] Split the code into segments: code resource is 28.3 KB of the ~32 KB single-segment limit
- [ ] UI: contacts, DMs, channels
- [x] Install method confirmed: HotSync over the USB cradle from the dev Mac (`make install`)

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
- macOS USB sync to the Visor works with pilot-link from desrod's repo. Fixed a race where macOS holds the Visor briefly when it appears, so pilot-link's single open attempt failed with exclusive access. The retry patch is saved in `palm-client/tools/`.
- First on-device run: VisorMesh v0.1 installed with `make install` and launches on the Visor Neo.
- Firmware builds and runs. Fixed the patch's I2C override (`-U` flags broke Wire under PlatformIO; now `build_unflags`). Pinned native GCC 14 on Apple Silicon. Enabled the USB companion link alongside the UART. The XIAO answers DEVICE_QUERY over USB, and that real frame is now a Palm host test.
- UART path verified: FT232R on D6/D7 at 115200 8N1 gets the same DEVICE_INFO as USB. The radio side of SB-LoRa is proven; what remains is the carrier board (SC16IS740 + bus) and the Palm transport for it.
- Protocol layer for the read-only commands, built against MeshCore's MyMesh.cpp and captured replies (DEVICE_QUERY, APP_START → SELF_INFO, GET_CONTACTS with 0 contacts, SYNC → NO_MORE_MESSAGES). LE fields are assembled byte by byte for the big-endian 68k. The radio is on MeshCore factory defaults (869.618 MHz / 62.5 kHz / SF8, 22 dBm, no name).
- **First end-to-end round trip from the Visor:** DEVICE_QUERY from VisorMesh on the Neo went over USB, through the Mac bridge and the FTDI adapter, into the XIAO's UART, and DEVICE_INFO came back and decoded on the Visor. `HsUd` turned out to use the Visor's HOTSYNC USB port (2); the endpoints stall around open/close and need clear-halt.
- Connect screen verified on the Neo: shows the radio's real SELF_INFO/DEVICE_INFO (9B1E4F92, 869.618 MHz, BW 62.5 SF8 CR5, 22 dBm, v1.17.1). Fixed a hand-typed DEVICE_INFO fixture that had two bytes shifted.
- Node Settings verified from the Visor: radio now 919.500 MHz / BW 500 / SF10 / CR5, 2-byte path hash (confirmed independently over the XIAO's USB). Seen in the bridge log: the Visor's USB link dropped while the Settings form was open and re-attached on the next send; it worked, but commands only try once, so this needs a closer look.
- Reception was dead until the XIAO rebooted: SET_RADIO_PARAMS saves and reports the new settings, but the receiver only retunes on a restart. Settings now offers to reboot the radio (CMD_REBOOT) after a radio change and reconnects. The radio then heard the user's contact.
- MeshTerm-style UI built: generated 4x6 font (PilRC's FONT compiler writes a corrupt NFNT, so gen_font.py writes it), rowlist, text layout (host-tested), message store and sync, Home/Conversation/Contacts/Node screens. Font verified readable on the Neo.
- Transcript wrapped after ~4 characters on the Visor: prc-tools puts `const` data in the code section, but another file reaching an `extern const` table addresses it A5-relative as data, so it read garbage widths. Shared tables (font widths, presets) are now non-const, set in their generators. Host tests can't catch this. Rule: never share an `extern const` table across files in palm-client.
