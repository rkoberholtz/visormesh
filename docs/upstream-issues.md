# Upstream issues

These are problems found while building VisorMesh that belong to other projects. Each entry has the evidence, our workaround, and a suggested fix, ready to turn into an upstream report. None have been filed yet.

## pilot-link (github.com/desrod/pilot-link)

### 1. macOS: first `USBDeviceOpen` races the system driver; Visor HotSync fails

**Seen on:** macOS 27, Apple Silicon. Handspring Visor Neo (0x082d:0x0100). pilot-link `6d696653`.

**Symptom:** pressing HotSync makes the Visor report "The connection between your handheld computer and the desktop could not be established". `pilot-xfer -p usb:` keeps listening.

**Evidence** (debug build, see item 2):
```
darwinusb: Accepted USB device, vendor: 0x082d product: 0x0100
darwinusb: unable to open device (kr=0xe00002c5)
```
`0xe00002c5` is `kIOReturnExclusiveAccess`. macOS's own USB driver holds the device for a moment after it appears. `device_added()` tries the open once and gives up. It only succeeds if macOS happens to announce the device a second time before the Visor times out, and that's intermittent.

**Workaround / fix:** retry `USBDeviceOpen` on `kIOReturnExclusiveAccess`, every 100 ms for up to 2 s. Our patch is `palm-client/tools/pilot-link-darwin-usb-open-retry.patch`. With it, HotSync and installs work reliably.

### 2. `darwinusb.c` can never log: it doesn't include `config.h`

`PI_DEBUG` is defined in `config.h`. `darwinusb.c` includes `pi-debug.h` without it, so every `LOG()` in the file compiles to nothing. `PILOT_DEBUG=DEV` prints nothing from the macOS USB backend, and the built library has 0 `darwinusb:` strings against 59 with the flag.

**Workaround:** build with `CPPFLAGS="-DPI_DEBUG=1"`.

**Fix:** `#include "config.h"` (or `pi-source.h` first) in `darwinusb.c`. Separately, `DEBUG_USB` is `#undef`ed right after being defined, which looks like a leftover.

### 3. Docs: tools need `--enable-conduits`

A bare `./configure` builds only the libraries: there's no `pilot-xfer`. The README says so in passing, but the quick start uses `pilot-xfer` without mentioning the flag.

## prc-tools-remix / PilRC (github.com/jichu4n)

### 4. PilRC 3.2.91 (amd64 .deb) writes a corrupt `FONT` (NFNT) header

**Repro:** a two-glyph font file (`fontType 36864`, `ascent 5`, `descent 1`, `GLYPH 65`, `GLYPH 66`, `GLYPH -1`) compiled with `pilrc` gives a header of
```
9000 0042 0000 0004 000f 0001 0001 0000 0000 ...
```
The firstChar/lastChar words are missing, the later fields are shifted, and `fRectHeight` is 0. Palm OS draws garbage with it. This looks like a 64-bit portability bug in the NFNT writer.

**Workaround:** `palm-client/tools/gen_font.py` writes the NFNT itself (with a round-trip check), and PilRC includes it as a `DATA "NFNT"` resource.

### 5. Toolchain notes (documentation, not bugs)

These are notes for anyone using multiple code sections:
- **A `const` table shared across files breaks at run time.** GCC 2.95 for Palm puts `const` data in `.text`, but another file reaching it through an `extern const` declaration addresses it A5-relative, as if it were data. It reads garbage, and host tests can't see it.
- **`-fwritable-strings` doesn't help.** It moves literals to `.data` but still addresses them PC-relative, which is wrong from any section.
- **What does work, checked in the compiled code:**
  - Literals in a function are emitted into that function's own section.
  - A file's `static const` data and calls into another section go through the `__text__`/`__text__<sect>` base.
  - Taking the address of a function in another section gives its real address.

Our rules and a link-time checker are in `palm-client/src/sections.h` and `palm-client/tools/check_sections.py`.

## MeshCore (github.com/meshcore-dev/MeshCore)

### 6. `CMD_SET_RADIO_PARAMS` saves and reports the new settings, but the receiver doesn't retune until a reboot

**Seen on:** companion firmware v1.17.1 (a366955c), Seeed XIAO nRF52840 + Wio-SX1262. Built with GCC 14; this hasn't been checked with the stock GCC 7 build.

**Repro:**
1. Change the frequency, bandwidth and SF from the factory 869.618 MHz to 919.500 MHz, 500 kHz, SF10 with `CMD_SET_RADIO_PARAMS`.
2. The reply is `RESP_OK`, and `SELF_INFO` reports the new values.
3. Nothing is received for about 4 hours: `CMD_GET_STATS` packets shows recv 0, recv errors 0, rx airtime 0, and noise floor -120.
4. After `CMD_REBOOT`, the node immediately hears adverts on the same settings.

**Workaround:** VisorMesh offers a radio reboot after a radio-parameter change.

**To check:** whether `radio_driver.setParams()` on the SX1262 needs a receive restart, or a standby → RX cycle, after a live retune.

### 7. A wrong client clock is effectively permanent: it persists through contact `lastmod`s

**Background:**
- `CMD_SET_DEVICE_TIME` only moves the clock forward, a sensible guard against replay.
- At boot, `BaseChatMesh::bootstrapRTCfromContacts()` sets the clock from the newest contact `lastmod`.

**What happens:** if a client ever sets the clock in the future (ours did, from a mistyped Palm date of Oct 2027), contacts heard meanwhile get future `lastmod`s. Every reboot restores the future time, and the client can't set it back.

**Recovery we used:** rewrite the affected contact's `lastmod` with `CMD_ADD_UPDATE_CONTACT` (using the optional `last_mod` field), then reboot.

**Ideas:**
- Ignore `lastmod`s far beyond the newest `last_advert_timestamp` when bootstrapping.
- Or provide an explicit "clock reset" command for companions.

### 8. `RESP_CODE_CHANNEL_INFO` for an unused slot carries stale bytes

`CMD_GET_CHANNEL` on an empty slot returns an empty name, but the remaining 47 bytes are left over from the previous reply in the reused buffer (e.g. `00 75 62 6c 69 63 ...`, "Public" with its first byte cleared). `companion_protocol.md` says an unused slot has "empty name and all-zero secret". So a client that checks the secret, as the docs suggest, sees every slot as used.

**Workaround:** treat an empty name as unused.

**Fix:** `memset` the reply (or the `ChannelDetails`) before filling it.
