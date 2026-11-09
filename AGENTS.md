# AGENTS.md

Guidance for coding agents working in this repo. Read it before you start. Also read [README.md](README.md), [docs/design.md](docs/design.md) and the end of [docs/progress.md](docs/progress.md): progress.md has the dated log of what's done and verified.

VisorMesh is a MeshCore client for the Handspring Visor Neo (Palm OS 3.5.2H). The Visor is a terminal for a MeshCore companion radio (XIAO nRF52840 + Wio-SX1262), which will sit on a custom Springboard module (SB-LoRa). Until those boards arrive, the Visor reaches the radio over its USB cradle through a bridge on the Mac.

## Working agreement

- **Commits:** commit finished work locally, one topic per commit. The message starts with the area (`palm-client: ...`, `docs: ...`, `firmware: ...`) and describes the change and the reason for it. Look at `git log` for the style.
- **Pushing:** only push when the user asks.
- **Device installs are the user's job.** Run `make`, never `make install`. Installing needs someone to press HotSync on the Visor. When a build is ready, say so and list exactly what to check on the device.
- **Recording progress:** add a dated entry to `docs/progress.md` only after the user confirms something works on the device. Say what was verified, what was found and fixed on the device, and the current segment sizes. Code that hasn't been verified on the device is described as untested.
- **The radio is on a live mesh (PhillyMesh).** Ask before anything that transmits: messages, adverts, logins, flood sends. Also ask before radio settings writes or reboots the user didn't request. Read-only queries over the XIAO's USB port are fine.
- **Radio settings are intentional:** 919.5 MHz, BW 500, SF10, CR5, 2-byte path hash. Don't "fix" them to MeshCore defaults.
- **Upstream issues** in `docs/upstream-issues.md` are filed only with the user's OK.
- **Don't commit the user's working files** unless asked. That includes the icon art at the repo root (`meshcore-visor-hair*.png/.svg`).
- **Reports from the user** are often short notes or photos in the middle of a task. Fix each one, include it in the next build, and keep going.
- **Before deleting or overwriting something you didn't create, look at it.** Ask if you're unsure whose it is.

## Build and test

```sh
cd palm-client
make          # build/visormesh.prc (in the visormesh-palm Docker image if m68k-palmos-gcc isn't installed)
make test     # host-side tests (protocol, text layout, scopes, map, SB-LoRa simulation)
make map      # Map basemap PDB (tools/make_map.py); install-map is the user's, like install
```

- **Docker hangs:** Docker Desktop's amd64 emulation sometimes hangs. `docker run` never starts, or a build sits there for minutes. Don't keep retrying. Kill the stuck `make`/`docker run` processes and ask the user to restart Docker. The host tests can also run natively with `cc -std=c99` against `src/protocol.c`, `src/sha256.c` and `src/remote_fmt.c` (see the `test` target).
- **After every link:** `tools/check_sections.py` runs automatically and fails the build on cross-segment references it can catch. A clean build means no warnings: treat a new warning as a bug.
- **Screen edges:** `tools/check_ui.py` also runs on every build. It fails when a form object or label goes past its form or runs into another, when an alert's buttons don't fit across it, or when a literal string drawn with `DrawCut` would be cut (use `DrawWrap` for sentences). For text built at run time, put the line's format in `src/screen_fmt.c` and give `tests/test_screens.c` its longest values. `tools/check_source.py` also runs on every build. It catches function-level `static const` in extra segments, menu items with no handler, clashing shortcuts, forms with no event handler, and repeated IDs.
- **Segment sizes** matter (see below). Check them with `m68k-palmos-objdump -h build/visormesh` inside the image.

## Bench setup

- **The Visor and the radio:** the Visor Neo sits in its USB cradle on the Mac. The XIAO radio is on USB-C (`/dev/cu.usbmodem*`), and an FTDI adapter on its D6/D7 UART is at `/dev/cu.usbserial-*`. Port names change after replugging.
- **The bridge:** `palm-client/tools/bridge.sh start|stop|status|log` runs `visor_bridge.py`, which joins the Visor's USB link to the radio's UART. Its log (`$TMPDIR/visor_bridge.log`) shows every frame in both directions. It's the first place to look when something on the device doesn't behave.
- **Shared USB port:** the bridge and HotSync can't share the Visor's USB port; `make install` stops the bridge first.

## Palm OS constraints (these have all bitten us)

The app is C for GCC 2.95 (prc-tools-remix) and PilRC, against the Palm OS 3.5 SDK, on a 68k with 16 grays at 160×160.

**Code segments.** The code is more than one 32 KB code resource, so modules live in `vmcode2`–`vmcode5`. Read `src/sections.h` before adding code; the rules are there. In short:

- Put `VM_SECT2/3/4` on a module's header prototypes, and on its static functions' forward declarations at the top of the file. The function body sits under its declaration.
- Never share an `extern const` table across files. The other file addresses it as data, but it's in code. Shared tables must not be const.
- A `static const` table inside a function in an extra segment reads as garbage on the device. File-level static const is fine. Otherwise compute the value or use a resource.
- `vmcode2` holds the core screens (23.6 KB as of 2026-11-05, after Node, Settings, Font Preview and Add Channel moved to `vmcode4`). Put new code in `vmcode5` (or `vmcode3`), and keep `vmcode2` for small hooks.

**Launches without globals.** Alarm, display-alarm, reset and sync-notify launch codes run without globals (no A5), and the extra segments aren't loaded. Code on those paths must be in segment 1 and touch no globals and no `VM_SECT*` functions. Check the disassembly when in doubt. A pointer table needs relocations: use a `switch`. `src/bgcheck.c` shows the pattern: it sublaunches the app with `sysAppLaunchFlagNewGlobals` for the real work.

**Other traps:**

- During any launch, `SysCurAppDatabase` returns the launched app, not the one in front. To tell whether the app is open, use a feature (`BgSetAppOpen`).
- The channel and contact databases open lazily. `ChannelsOpen` is only called by `ChannelsRefresh` and `ConvosBuild`. Paths that skip those must open the databases themselves, or every lookup quietly fails.
- With no module in the slot, accessing the Springboard slot's address space is a bus error that resets the Visor. Check `HsCardAttrGet(hsCardAttrHwInstalled)` first (`SbSlotState`). A saved Slot link is only restored at launch while the OS reports a module.
- `StrPrintF` hex output printed zeros. Use `TxtHex` (ui/text.c).
- The Visor's clock is local time with no time zone. Everything stored and sent is UTC via the app's UTC-offset preference (`SessionNow`, `SessionLocalPalm`).
- 68k is big-endian; the companion protocol is little-endian. Assemble LE fields byte by byte (see `protocol.c`).
- PilRC can't read compressed BMPs. `tools/make_icon.sh` writes them with `-compress None`.
- The USB link drops when the Visor sleeps. The app reconnects on wake (`CheckWake` in main.c).
- Interrupt handlers (`transport_sb.c`) may only access memory, use no globals, and must sit in segment 1.

**References:** `docs/reference/` has the Handspring SDK (`HsExt.h`, Springboard docs, the Visor technical reference) and the Springboard specs. The Palm OS 3.5 SDK headers are inside the Docker image; `make editor` copies them to `palm-client/.sdk/` for your editor and for grepping.

## Code style

- **Match the surrounding code:** tabs, K&R braces, `Vm`/`VM_` prefixes for project types and constants, Palm OS types (`UInt16`, `Char`, `Err`), and `gName` for file globals.
- **Comments:**
  - Each header opens with a short comment saying what the module is for and the rules a caller needs.
  - Functions get one-line comments when the reason isn't obvious from the code.
  - Comments explain why, not what. Match the existing density.
- **UI:** stay close to Palm conventions. Use the standard menus (Go / Channel / Options, one menu bar per screen), alerts and popup lists. Extras are welcome where they help, such as tap-and-hold on list rows via the `RowList` hold callback, but anything reachable by a gesture must also be in a menu.
- **Resources:** in `resources/visormesh.rcp`, with IDs in `src/resources.h`, grouped by form.
- **Tests:** pure-C layers (protocol, text layout, scope keys, map geometry, SB-LoRa simulation) have host tests in `palm-client/tests/`. Add or extend them when you change those layers. UI and Palm API code can only be verified on the device.
- **Docs and user-facing text:** plain, short and concrete. The docs use British spelling (colour, neighbours).

## Hardware and firmware

- **SB-LoRa:** the KiCad board is in `hardware/sb-lora/`. Hardware revisions are tagged in git (`hw-v0.1`, ...) so the fab files stay recoverable.
- **Firmware:** `firmware/` holds the MeshCore companion patch for the XIAO (UART on D6/D7 alongside USB), with its own README and Makefile.
- **Flashing:** flashing or rebooting the radio counts as a radio write. Ask first.
