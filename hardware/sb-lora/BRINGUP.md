# SB-LoRa v0.1 bring-up

A step-by-step plan for the first assembled board. Each step only needs the steps before it to have passed, so stop at the first failure and use the table under it. The README has the design; this is the order to prove it in.

What you need: a multimeter, a current-limited bench supply (3.3 V, limit about 20 mA), a scope or frequency counter if you have one, the Visor Neo with VisorMesh installed, and the XIAO + Wio stack from the bench.

## 1. Bare board, before soldering anything

| Check | Expect | If not |
|---|---|---|
| JLC preview / photos: pin 1 of U1-U7, Y1 and D1 on the silkscreen triangle | All match | Don't power it. A rotated SC-70 or the TSSOP is the likeliest fault |
| VSB (J1 pads 17, 51) to GND (1, 34, 35, 68) | Not a short (> 1 kΩ, rising as C1 charges) | Look for a solder bridge on U1-U7 or C1-C5 |
| 17 to 51, and 1 / 34 / 35 / 68 to each other | Continuity | A broken trace or pad |
| CD1* (36) and CD2* (67) to GND | Continuity | The Visor won't see the module (step 3 says "Slot: empty") |
| VDOCK (18, 52) to anything | Open, with JP1 open | Leave JP1 open (README, VDOCK) |
| Dry-fit the bare board in the slot | Goes in fully; the extension clears the Visor body | Note how far off it is before fitting J1 |

## 2. J1 fitted, bench power

Fit J1 only (check its board offset against a (51) part if it's harvested). No XIAO yet.

| Check | Expect | If not |
|---|---|---|
| 3.3 V into VSB (17/51), current limit 20 mA | A few mA | Off at once if it hits the limit: a reversed or shorted part |
| Y1 with a 10x probe or counter | 14.7456 MHz | C6/C7 or Y1 itself. SPI register access should still work without it, but step 3's loopback won't |
| U4 pin 1 (VCC) | 3.3 V | |

## 3. In the Visor, no XIAO: the SB-LoRa Test

1. In VisorMesh, keep the link on **USB** for now (Node), and don't connect.
2. Insert the module. Wait 3 s: the Visor looks for a module ROM, which this board doesn't have.
3. **Options > SB-LoRa Test**. Take a screenshot (Options > Screenshot in 5 s, then `make shots` in `palm-client/`) and keep it.

The test's lines, in order. It stops at the first failed step.

| Line | Good | Bad, and what it points to |
|---|---|---|
| `Slot: CS0 at ..., ... KB` | Any address (expected `28000000`) and size | `Slot: empty`: the OS sees no module: CD1*/CD2* (36/67) not grounded at J1, or J1 not seated. `a module, but no chip selects`: report it; the OS gave no window, which needs code changes |
| `A0 = CPU A1: 55AA read ...` and `A0 = CPU A0: ...` | One of the two ends `, ok` | Both fail: see the read values below. **Note which one passed**: it settles whether the connector's A0 is CPU A1 (expected) or A0 |
| `UART: LCR LSR ..., TX room ...` | `0360`, `64`, `, ok` | The scratch register passed, so the bus works: a wrong value here means reads are only sometimes right (marginal timing, a weak MISO through R1). Run it a few times and note what changes |
| `Loopback: 64 bytes back, ok` | | `0 of 64 back`: the UART has no clock: Y1/C6/C7 (step 2). Bytes back but wrong: the wrong clock frequency |
| `Interrupt: works, 64 of 64 bytes` | | `none came. Check IRQ*.`: U4 pin 7 to J1 pin 16 (IRQ*). `Polled (no interrupt handler)`: the OS refused the handler: report it; the link still works, polled |
| `Radio: ...` | `no answer` is expected here: there's no XIAO yet | |

Reading the mapping values (what came back for 55 then AA):

| Read | Likely cause |
|---|---|
| `ffff` | MISO never driven: U4 not selected (U1, A17 latch) or not powered |
| `0000` | MISO or D0 stuck low (a short at R1), or no SCLK (U3) |
| `aa55` or another one-bit shift | MISO sampled on the wrong edge or MOSI latched late: U2 (A8 latch) or U3 |
| Changes every run | D0 floating: R1 or the D0 pad (J1 pin 30) |
| Right for one mapping only | That's the answer; the other mapping reads junk by design |

## 4. The XIAO stack, before it goes on the carrier

The bench XIAO already runs the `Xiao_nrf52_companion_radio_uart` build (`firmware/`) with the PhillyMesh settings (919.5 MHz, BW 500, SF10, CR5, 2-byte path hash), and answers DEVICE_QUERY on D6/D7.

- **Disconnect the FTDI adapter from D6/D7 first.** Left on, it fights the carrier's translators.
- Solder the LiPo to BAT+/BAT- if it isn't already. The carrier never charges it; USB-C does.
- Check the stack's pins against the silkscreen labels (D0..D6, 5V, GND, 3V3, D10..D7). Mirrored labels mean the stack is upside down.

## 5. The whole module

1. Plug the stack in, insert the module, and run the SB-LoRa Test again.
2. The last line should be `Radio: Seeed Xiao-nrf52 v1.17.1` (or whatever the firmware reports).

| Last line | Likely cause |
|---|---|
| `Radio: no answer. Powered? Firmware?` | The XIAO isn't running (LiPo flat, try USB-C), D6/D7 swapped, or a translator: U6 carries XIAO D6 to the bridge's RX, U5 the bridge's TX to D7, both need 3V3X from the XIAO |
| `Radio: answered, but the reply didn't parse.` | Bytes get through but some are corrupted: baud mismatch (crystal) or noise on D6/D7 |
| `sending stuck (TX FIFO)` | The UART isn't clocked (step 2) |

## 6. Using it

1. On Node, set the link to **Slot** and Connect. Conversations, contacts and Radio Stats should behave as they do over USB. The Slot link stays up while the Visor sleeps, so there's no reconnect on wake.
2. Disconnect (or close VisorMesh) before pulling the module out.

## 7. Waking for messages

With the Slot link, a new message wakes the Visor:

- **App open, Visor asleep:** the app syncs within a few seconds and plays its sound, with the screen still off.
- **App closed:** set Preferences > Check when closed (any interval) before closing. A message starts the closed-app check at once, which syncs, plays the sound and shows the alert, instead of waiting for the next alarm. After a reset or a HotSync install, this starts again from the first regular check.

To test: close VisorMesh, turn the Visor off, and send a DM to the radio from another node (ask before sending on PhillyMesh). The Visor should chirp within a few seconds and show the alert. If it only does so at the next interval, the wake isn't working but the regular check is; note that and the screenshot of the SB-LoRa Test.

## 8. Standby current

The slot allows 100 µA while the Visor sleeps. While the app is closed the SC16IS740 sleeps between frames (IER bit 4) and wakes on RX. If you can measure VSB current: with the Visor off and the radio quiet it should be well under 100 µA. Every packet the radio hears and reports wakes the bridge briefly. Only "message waiting" wakes the Visor.

## What to send back

The SB-LoRa Test screenshot from step 3 and step 5, which mapping passed, the current from step 2, and anything that didn't match these tables. The mapping and chip-select address go into `palm-client/src/sb_spi.c` (the probe can then skip the wrong one) and `docs/progress.md`.
