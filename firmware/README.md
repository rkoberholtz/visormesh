# Firmware

## meshcore-xiao-nrf52-uart-companion.patch

Adds a `Xiao_nrf52_companion_radio_uart` PlatformIO env to MeshCore. It runs the companion protocol on the XIAO nRF52840's hardware UART, D6 (TX, P1.11) / D7 (RX, P1.12), at 115200 8N1, and moves I2C to unused internal pins 16/17.

```sh
cd firmware
make                 # clone MeshCore @ MESHCORE_REF into meshcore/, apply the patch, build
make upload          # flash the XIAO over USB
make MESHCORE_URL=~/Code_Repo/MeshCore-Stock   # clone from a local checkout instead of GitHub
```

The patch is generated against MeshCore `a366955c` (2026-09-30), pinned as `MESHCORE_REF` in the `Makefile`. `meshcore/` is a git-ignored build checkout.

I2C is moved off D6/D7 with `build_unflags`. A `-U` flag in `build_flags` doesn't work under PlatformIO: it collects `-D` defines together and emits `-U` after them, which erases the new value too.

On Apple Silicon without Rosetta, the nRF52 platform's default GCC 7.2.1 (x86_64-only) won't run. The Makefile pins PlatformIO's native GCC 14.2.1 for the build checkout instead (`TOOLCHAIN_PIN`). Stock MeshCore nRF52 releases use GCC 7, so treat GCC 14 builds as untested until they've run on hardware.

Verified: builds cleanly (RAM 60.8%, flash 54.0%); the compile flags show `PIN_WIRE_SCL=16`, `PIN_WIRE_SDA=17`, `SERIAL_TX=D6`, `SERIAL_RX=D7`.
