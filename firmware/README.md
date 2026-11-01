# Firmware

## meshcore-xiao-nrf52-uart-companion.patch

Adds a `Xiao_nrf52_companion_radio_uart` PlatformIO env to MeshCore. It runs the companion protocol on the XIAO nRF52840's hardware UART, D6 (TX, P1.11) / D7 (RX, P1.12), at 115200 8N1, for the Visor. It also keeps the USB link (`ENABLE_USB_INTERFACE`) for bench testing and setup with meshcore-cli or the MeshCore app. I2C moves to unused internal pins 16/17.

```sh
cd firmware
make                 # clone MeshCore @ MESHCORE_REF into meshcore/, apply the patch, build
make upload          # flash the XIAO over USB
make MESHCORE_URL=~/Code_Repo/MeshCore-Stock   # clone from a local checkout instead of GitHub
make upload UPLOAD_PORT=/dev/cu.usbmodemXXXX   # pick the port explicitly
```

The patch is generated against MeshCore `a366955c` (2026-09-30), pinned as `MESHCORE_REF` in the `Makefile`. `meshcore/` is a git-ignored build checkout.

I2C is moved off D6/D7 with `build_unflags`. A `-U` flag in `build_flags` doesn't work under PlatformIO: it collects `-D` defines together and emits `-U` after them, which erases the new value too.

On Apple Silicon without Rosetta, the nRF52 platform's default GCC 7.2.1 (x86_64-only) won't run. The Makefile pins PlatformIO's native GCC 14.2.1 for the build checkout instead (`TOOLCHAIN_PIN`). Stock MeshCore nRF52 releases use GCC 7, so treat GCC 14 builds as untested until they've run on hardware.

Verified: builds cleanly (RAM 60.8%, flash 54.0%); the compile flags show `PIN_WIRE_SCL=16`, `PIN_WIRE_SDA=17`, `SERIAL_TX=D6`, `SERIAL_RX=D7`.

Verified on hardware (2026-11-01): flashed to the SB-LoRa XIAO (serial `87F2365FB1415308`). Over USB it answers `CMD_DEVICE_QUERY` with DEVICE_INFO: MeshCore v1.17.1, "Seeed Xiao-nrf52". The D6/D7 UART path still needs a 3.3 V USB-TTL adapter to test.

## tools/companion_query.py

Sends a companion frame and prints the reply frames. Uses only the Python standard library.

```sh
tools/companion_query.py /dev/cu.usbmodemXXXX            # CMD_DEVICE_QUERY over the XIAO's USB
tools/companion_query.py /dev/cu.usbserial-XXXX 16 03    # over a USB-TTL adapter on D6/D7
```
