# Firmware

## meshcore-xiao-nrf52-uart-companion.patch

Adds a `Xiao_nrf52_companion_radio_uart` PlatformIO env to MeshCore. It runs the companion protocol on the XIAO nRF52840's hardware UART, D6 (TX, P1.11) / D7 (RX, P1.12), at 115200 8N1, and moves I2C to unused internal pins 16/17.

```sh
git clone https://github.com/meshcore-dev/MeshCore && cd MeshCore
git apply /path/to/visormesh/firmware/meshcore-xiao-nrf52-uart-companion.patch
pio run -e Xiao_nrf52_companion_radio_uart -t upload
```

The patch was written against a MeshCore checkout from October 2026. If it no longer applies, check `examples/companion_radio/main.cpp` and `variants/xiao_nrf52/platformio.ini` for changes upstream. Record the MeshCore commit it applies cleanly to here once you've built it.
