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
git checkout 6d696653   # the commit the patch below was made against
git apply /path/to/visormesh/palm-client/tools/pilot-link-darwin-usb-open-retry.patch
./autogen.sh --prefix=$HOME/.local --enable-conduits && make -j"$(sysctl -n hw.ncpu)" && make install
```

**The patch is needed for the Visor on macOS.** When the Visor appears on USB after HotSync, macOS's own USB driver holds it for a moment. Unpatched pilot-link tries to open it once, fails with `kIOReturnExclusiveAccess` (`0xe00002c5`), and the Visor shows "The connection between your handheld computer and the desktop could not be established". The patch retries the open for up to 2 seconds.

To debug USB on macOS, add `CPPFLAGS="-DPI_DEBUG=1"` to `make`, then run with `PILOT_DEBUG="DEV SLP" PILOT_DEBUG_LEVEL=DEBUG`. Without that flag, `darwinusb.c` logs nothing, because it doesn't include `config.h`.

The Makefile uses `~/.local/bin/pilot-xfer` when it exists. On Linux, use the kernel `visor` driver and `make install PORT=/dev/ttyUSB1`.

Verified: `pilot-xfer -p usb: -l` lists the Visor Neo's databases from macOS on Apple Silicon. The Visor is behind a USB hub, so a hub works.

## A map of your own area

The Map screen's basemap is a separate database, `VisorMeshMap.pdb`. `make map` builds the Philadelphia one. For any other place, run the Map Picker. It works on macOS, Windows and Linux, and needs only Python 3:

```sh
python3 tools/map_picker.py
```

It opens a page in the browser.

1. **Choose the area:** drag the blue box over the area to cover (main roads, water and parks at every zoom), and the orange box over the part that gets every street at the closest zoom. Or pan the map to a place and use **Fit to view**.
2. **Check the size:** the page shows each zoom's tiles and an estimate of the file's size. Street level is most of it, and the Visor Neo has 8 MB for everything.
3. **Build:** **Build Map** downloads the OpenStreetMap data from the public Overpass server in 0.25° squares, then draws the tiles. The data is cached in `~/.visormesh/map-cache`, so each place is only fetched once. The server is sometimes busy, and the picker waits and tries again.
4. **Install:** the map is saved in `~/VisorMeshMap` and offered as a download. Install it with `pilot-xfer -p usb: -i VisorMeshMap.pdb` (or `make install-map` after copying it to `build/`), or with Palm Desktop's Install Tool on Windows. It replaces the map on the Visor.

## Testing against a real radio over USB (before the boards arrive)

`tools/visor_bridge.py` connects the app to a MeshCore companion radio through the Visor's USB cradle:

```sh
tools/visor_bridge.py /dev/cu.usbserial-XXXX    # radio's D6/D7 UART on a 3.3 V USB-TTL adapter
tools/visor_bridge.py /dev/cu.usbmodemXXXX      # or the XIAO's own USB companion link
```

In VisorMesh, set **Link** to **USB** and tap **Connect**. The app opens the Neo's `'HsUd'` Serial Manager driver, so the Visor appears on USB. The bridge reads the Visor's port list (vendor request `0x03`), forwards bytes to the radio, and logs every companion frame in both directions. What we found while getting it working:

- `HsUd` sends on the Visor's HOTSYNC USB port (2), not GENERIC (1). The bridge listens on every port and replies on whichever one the Visor used.
- The endpoints stall briefly around open and close, so the bridge clears them (`libusb_clear_halt`) and carries on.
- The Visor only appears on USB after `SrmOpen`. The app waits a second, then retries sends that time out (`serErrTimeOut`, 0x0305) while the bridge attaches.
- The bridge grabs the Visor during a HotSync too, so `make install` stops it first.

The Ports screen lists the Neo's Serial Manager drivers: `A8xZ` Serial, `HaSA` USB/Serial, `HsUc` USB Console, `HsUd` USB, `HsUr` USB RemoteFS, and more.

## Testing without hardware

The Visor can't be emulated on a Mac: CloudpilotEmu doesn't support Handspring devices. Use CloudpilotEmu (https://cloudpilot-emu.github.io/app) with a Palm OS 3.5 device ROM, such as a Palm IIIx or m100. The OS APIs match the Neo's. In the emulator, Connect should report "No reply from radio" or a port error, since there's no radio attached.
