# Design notes

Decisions and background carried over from the original planning conversation (claude.ai, October 2026).

## Target device

- Handspring Visor, Palm OS **3.5.2H3.0** (Handspring 3.5 build), DragonBall CPU, runs on 2x AAA.
- No BLE or WiFi. The radio is never powered from the AAAs: an SX1262 at 22 dBm TX draws over 100 mA.

## Architecture

The Visor is a dumb terminal for a MeshCore companion radio. Running Ed25519/X25519 on a 16 to 33 MHz DragonBall would be painfully slow. The companion protocol is simple byte framing, which the Visor handles easily.

### Companion protocol (serial)

- 115200 8N1. The radio is the server and the Visor app is the client.
- Radio → app frames start with `0x3E` (`>`). App → radio frames start with `0x3C` (`<`). Each is followed by a 2-byte little-endian length.
- `MAX_FRAME_SIZE` = 172 bytes.
- Minimal client command set: `APP_START`, `GET_CONTACTS`, `SYNC_NEXT_MESSAGE`, `SEND_TXT_MSG`, the channel-message equivalents, and handling of the `0x8x` push codes.
- First bring-up probe: `CMD_DEVICE_QUERY` = `<` + len + `0x16 0x03`, expect a `>`-framed DEVICE_INFO reply.
- Reference: Companion Radio Protocol page on the eastmesh wiki.

## Transport options considered

1. **Cradle-connector serial**: 3.3 V TTL UART, no level shifter needed. Requires companion firmware on a hardware UART instead of USB-CDC, since the Visor can't be a USB host. Verify the pinout with a scope first, because sources disagree. Don't wire the HotSync line.
2. **Raw IR (SIR)**: `SrmOpen(serPortIrPort, ...)` paired with an MCP2120 and a TFDU transceiver on the radio. Needs line of sight.
3. **Intel/Xircom SpringPort Wireless Ethernet** (802.11b) to a WiFi companion on TCP 4000. Rare module and WEP-era, so a stunt rather than a daily driver.
4. **Custom Springboard LoRa module**: **chosen**, now the SB-LoRa board. The Palm-side protocol code is the same as option 1. Only the transport routines differ.

## SB-LoRa module (hardware/sb-lora)

- XIAO nRF52840 + Wio-SX1262 stack on 1x7 pin headers, running MeshCore companion firmware with the UART on D6 (TX) / D7 (RX). See `firmware/`.
- The Visor reaches it through bus-SPI (springSD-style CS0/CS1 latch trick) into an **SC16IS740** SPI-UART bridge. A 14.7456 MHz crystal gives 115200 baud (DLL=8).
- Power is isolated: only the SN74LVC1T45s span the Visor and XIAO supplies. The LiPo is soldered to the XIAO BAT pads and charged by the XIAO over USB-C.
- VDOCK charging sits behind JP1, which is left **open**.
- There's no module ROM in v0.1, so the client installs to RAM.
- The springSD v1.1 connector footprint and bus routines (Dmitry Grinberg) are the basis for J1 and the 68k bus code.

## Palm client plans

- Toolchain: prc-tools (m68k gcc) or CodeWarrior, Palm OS 3.5 SDK, plus Handspring HsExt headers for Springboard notifications.
- Use the New Serial Manager (`Srm*`) for cradle and IR. Use `SrmReceiveWindowOpen/Close` for zero-copy parsing, and `SrmSetWakeupHandler` + `SrmPrimeWakeupHandler(portId, 3)` to wake on the frame header.
- On SB-LoRa, the transport is the bus-SPI routines driving the SC16IS740 (IRQ* on the Springboard interrupt line). Put the SC16IS740 to sleep (IER bit 4) when idle to stay under the 100 µA standby limit.
- Power model: let the Visor sleep. The companion queues messages, and on wake the client drains with `SYNC_NEXT_MESSAGE` until NO_MORE_MESSAGES.
- Store contacts and messages in Palm databases, not the heap. Set the backup bit so HotSync keeps a copy.
- Develop in POSE with a Visor ROM matching 3.5.2H3 (Platinum/Prism or Neo/Pro profile matching the unit). POSE can bridge its serial port to a real companion board, but it can't emulate the Springboard module.
- Install: HotSync (Palm Desktop 4.x in an XP VM, or `pilot-xfer -p /dev/ttyUSB1 -i visormesh.prc` on Linux), or the user's Springboard CF adapter if its ROM has an installer. The CF adapter and SB-LoRa share the single slot.

## Future (v0.2 ideas)

- Small flash on CS0 with a module ROM header, so the client auto-launches on insert with no install step.
