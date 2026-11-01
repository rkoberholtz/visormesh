# VisorMesh Palm client.
#
#   make           build build/visormesh.prc (in Docker if the toolchain isn't installed)
#   make test      run the host-side protocol tests
#   make install   copy the .prc to the Pi and install it over the USB cradle
#   make clean

APP     = visormesh
NAME    = VisorMesh
CREATOR = VMsh

SRCS = src/main.c src/protocol.c src/transport_srm.c
OBJS = $(SRCS:src/%.c=build/%.o)

CC      = m68k-palmos-gcc
CFLAGS  = -palmos3.5 -O2 -Wall -Isrc
PILRC   = pilrc

# Pi with the Visor cradle attached (see tools/pi-install.sh).
PI      ?= visormesh-pi
PI_PORT ?= /dev/ttyUSB1

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

install: build/$(APP).prc
	scp -q build/$(APP).prc $(PI):/tmp/$(APP).prc
	ssh -t $(PI) "bash -s -- /tmp/$(APP).prc $(PI_PORT)" < tools/pi-install.sh

clean:
	rm -rf build

.PHONY: all test image install clean
