# Palm client

The VisorMesh app for the Visor Neo (Palm OS 3.5.2H3.0). It's written in C with prc-tools-remix (GCC 2.95 for m68k) and PilRC, against the Palm OS 3.5 SDK.

## Build

```sh
make          # -> build/visormesh.prc
make test     # host-side tests for the protocol framing
```

If `m68k-palmos-gcc` isn't installed, `make` builds and runs the `visormesh-palm` Docker image (see `Dockerfile`) and does the build inside it. The first run takes a minute or two. The image is amd64-only, so on Apple Silicon it runs under emulation. That's fine for builds this small.

The toolchain versions and the palm-os-sdk commit are pinned in the `Dockerfile`.

Your editor's C checker can't see the Palm SDK headers, since they only exist inside the image. Errors like `'PalmOS.h' file not found` there are expected and don't affect the build.

## Code layout

| File | Layer |
|---|---|
| `src/transport.h` | Byte-stream link to the radio. One implementation per transport |
| `src/transport_srm.c` | Serial Manager transport (cradle serial port, or an emulator's serial port) |
| `src/protocol.[ch]` | MeshCore companion framing (`<`/`>` + 2-byte LE length). Pure C, so it's tested on the host |
| `src/main.c` | App startup, event loop, main form |
| `resources/visormesh.rcp` | PilRC forms and alerts. IDs are in `src/resources.h` |
| `tests/` | Host tests (`make test`) |

The SB-LoRa transport (bus-SPI into the SC16IS740) will be a second `transport_*.c` behind the same interface.

Creator ID is `VMsh`. The database has the backup bit set, so HotSync keeps a copy.

## Install on the Visor

The Visor's USB cradle plugs straight into the dev Mac. `make install` builds the `.prc` and runs `pilot-xfer -p usb: -i`. Press the HotSync button when it asks.

pilot-link isn't in Homebrew any more, so build https://github.com/desrod/pilot-link (maintained by pilot-link's original author) into `~/.local`. Its macOS USB backend uses IOKit directly, so no driver is needed:

```sh
brew install autoconf automake libtool pkg-config libpng readline popt
git clone https://github.com/desrod/pilot-link.git ~/src/pilot-link
cd ~/src/pilot-link
./autogen.sh --prefix=$HOME/.local --enable-conduits && make -j"$(sysctl -n hw.ncpu)" && make install
```

The Makefile uses `~/.local/bin/pilot-xfer` when it exists. On Linux, use the kernel `visor` driver and `make install PORT=/dev/ttyUSB1`.

**Not tested end to end yet.** pilot-link's macOS USB support was tested with a Tungsten E2, not a Visor.

## Testing without hardware

The Visor can't be emulated on a Mac: CloudpilotEmu doesn't support Handspring devices. Use CloudpilotEmu (https://cloudpilot-emu.github.io/app) with a Palm OS 3.5 device ROM, such as a Palm IIIx or m100. The OS APIs match the Neo's. In the emulator, "Query radio" should report "No reply" or an open error, since there's no radio attached.
