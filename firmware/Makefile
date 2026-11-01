# MeshCore companion firmware for the SB-LoRa XIAO nRF52840 + Wio-SX1262 stack.
#
#   make             clone MeshCore at MESHCORE_REF into meshcore/, apply the patch, build
#   make upload      build and flash the XIAO over USB (UPLOAD_PORT=/dev/cu.usbmodemXXXX to pick one;
#                    double-tap reset for the bootloader if it isn't found)
#   make monitor     serial monitor on the XIAO's USB port
#   make clean       remove the meshcore/ checkout
#
# MESHCORE_URL can point at a local clone to skip the download, e.g.
#   make MESHCORE_URL=~/Code_Repo/MeshCore-Stock

MESHCORE_URL ?= https://github.com/meshcore-dev/MeshCore.git
MESHCORE_REF ?= a366955c
PATCH        := meshcore-xiao-nrf52-uart-companion.patch
ENV          := Xiao_nrf52_companion_radio_uart
PIO          ?= pio

# The nRF52 platform's default toolchain (GCC 7.2.1) is x86_64-only and needs
# Rosetta on Apple Silicon. There, pin PlatformIO's native GCC 14 build instead
# by appending to our env, which is the last section of the XIAO's ini.
ifeq ($(shell uname -sm),Darwin arm64)
TOOLCHAIN_PIN = platform_packages = platformio/toolchain-gccarmnoneeabi@1.140201.0
endif

all: build

meshcore/.patched: $(PATCH)
	rm -rf meshcore
	git clone -q $(MESHCORE_URL) meshcore
	git -C meshcore checkout -q $(MESHCORE_REF)
	git -C meshcore apply ../$(PATCH)
ifneq ($(TOOLCHAIN_PIN),)
	echo "$(TOOLCHAIN_PIN)" >> meshcore/variants/xiao_nrf52/platformio.ini
endif
	touch $@

build: meshcore/.patched
	cd meshcore && $(PIO) run -e $(ENV)

upload: meshcore/.patched
	cd meshcore && $(PIO) run -e $(ENV) -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT))

monitor:
	cd meshcore && $(PIO) device monitor -b 115200

clean:
	rm -rf meshcore

.PHONY: all build upload monitor clean
