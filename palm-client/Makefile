# VisorMesh Palm client.
#
#   make           build build/visormesh.prc (in Docker if the toolchain isn't installed)
#   make test      run the host-side protocol tests
#   make install   install the .prc over the USB cradle (press HotSync when asked)
#   make editor    copy the SDK headers to .sdk/ and write compile_flags.txt for clangd
#   make clean

APP     = visormesh
NAME    = VisorMesh
CREATOR = VMsh

SRCS = src/main.c src/app.c src/home_form.c src/convo_form.c src/node_form.c src/channel_form.c src/convos.c src/protocol.c src/sha256.c src/transport_srm.c src/session.c src/settings.c src/presets.c src/contacts.c src/contacts_form.c src/messages.c src/channels.c src/preview_form.c src/ui/draw.c src/ui/text.c src/ui/rowlist.c src/ui/fontmetrics.c src/ports.c
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

build/%.o: src/%.c src/*.h src/ui/*.h | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Two code segments (see src/sections.h): multigen turns visormesh.def into a
# stub and a linker script that place section vmcode2 in its own code resource.
build/$(APP)-sections.s build/$(APP)-sections.ld: $(APP).def | build
	m68k-palmos-multigen -b build/$(APP)-sections $(APP).def

build/$(APP)-sections.o: build/$(APP)-sections.s
	$(CC) -c $< -o $@

build/$(APP): $(OBJS) build/$(APP)-sections.o build/$(APP)-sections.ld
	$(CC) $(CFLAGS) -o $@ $(OBJS) build/$(APP)-sections.o build/$(APP)-sections.ld

build/res.stamp: resources/$(APP).rcp src/resources.h resources/smallfont.nfnt | build
	rm -rf build/res && mkdir -p build/res
	$(PILRC) -q -I src $< build/res
	touch $@

build/$(APP).prc: build/$(APP) build/res.stamp
	build-prc -o $@ $(APP).def build/$(APP) build/res/*.bin

test: | build
	gcc -std=c99 -Wall -Wextra -o build/test_protocol tests/test_protocol.c src/protocol.c src/sha256.c
	build/test_protocol
	gcc -std=c99 -Wall -Wextra -o build/test_text tests/test_text.c src/ui/text.c src/ui/fontmetrics.c src/protocol.c
	build/test_text

build:
	mkdir -p build

endif

install: all
	@if pgrep -f tools/visor_bridge.py >/dev/null; then \
	  echo "Stopping visor_bridge.py (it would grab the Visor during HotSync)"; \
	  pkill -f tools/visor_bridge.py; sleep 1; fi
	@echo "Press the HotSync button on the Visor cradle..."
	$(PILOT_XFER) -p $(PORT) -i build/$(APP).prc

clean:
	rm -rf build

# Editor support: clangd can't see the SDK inside the image, so copy it out.
editor: image
	rm -rf .sdk && mkdir -p .sdk
	cid=$$(docker create --platform linux/amd64 $(DOCKER_IMAGE)) && \
	  docker cp -q $$cid:/opt/palmdev/sdk-3.5/include .sdk/sdk-3.5; docker rm $$cid >/dev/null
	printf '%s\n' -std=gnu89 -D__palmos__=1 -Isrc -Wno-implicit-int -Wno-pointer-sign \
	  -Wno-deprecated-non-prototype -Wno-incompatible-library-redeclaration \
	  -Wno-ignored-attributes -Wno-pragma-once-outside-header > compile_flags.txt
	find .sdk/sdk-3.5 -type d | sed 's|^|-I|' >> compile_flags.txt

.PHONY: all test image install clean editor
