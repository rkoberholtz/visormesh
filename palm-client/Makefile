# VisorMesh Palm client.
#
#   make           build build/visormesh.prc (in Docker if the toolchain isn't installed)
#   make test      run the host-side protocol tests
#   make install   install the .prc over the USB cradle (press HotSync when asked)
#   make clean

APP     = visormesh
NAME    = VisorMesh
CREATOR = VMsh

SRCS = src/main.c src/protocol.c src/transport_srm.c
OBJS = $(SRCS:src/%.c=build/%.o)

CC      = m68k-palmos-gcc
CFLAGS  = -palmos3.5 -O2 -Wall -Isrc
PILRC   = pilrc

# pilot-link built from source into ~/.local (see README). On macOS the
# cradle is "usb:"; on Linux with the visor driver it's /dev/ttyUSB1.
PILOT_XFER ?= $(firstword $(wildcard $(HOME)/.local/bin/pilot-xfer) pilot-xfer)
PORT       ?= usb:

DOCKER_IMAGE = visormesh-palm
DOCKER_RUN   = docker run --rm --platform linux/amd64 -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE)

ifeq ($(shell command -v $(CC) 2>/dev/null),)

all test: image
	$(DOCKER_RUN) make $@

image:
	docker build --platform linux/amd64 -q -t $(DOCKER_IMAGE) . >/dev/null

else

all: build/$(APP).prc

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

build/res.stamp: resources/$(APP).rcp src/resources.h | build
	rm -rf build/res && mkdir -p build/res
	$(PILRC) -q -I src $< build/res
	touch $@

build/$(APP).prc: build/$(APP) build/res.stamp
	build-prc --backup -o $@ -n "$(NAME)" -c $(CREATOR) build/$(APP) build/res/*.bin

test: | build
	gcc -std=c99 -Wall -Wextra -o build/test_protocol tests/test_protocol.c src/protocol.c
	build/test_protocol

build:
	mkdir -p build

endif

install: all
	@echo "Press the HotSync button on the Visor cradle..."
	$(PILOT_XFER) -p $(PORT) -i build/$(APP).prc

clean:
	rm -rf build

.PHONY: all test image install clean
