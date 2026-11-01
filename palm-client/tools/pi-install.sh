#!/bin/bash
# Runs on the Pi (piped over ssh by `make install`): installs a .prc onto the
# Visor through its USB cradle using pilot-link.
#
# Usage: pi-install.sh FILE.prc [PORT]
#   PORT defaults to /dev/ttyUSB1 (the kernel visor driver's HotSync port).
#   Use "usb:" instead if pilot-link was built with libusb.
set -euo pipefail

prc="$1"
port="${2:-/dev/ttyUSB1}"

if ! command -v pilot-xfer >/dev/null; then
	echo "pilot-xfer not found on $(hostname). Install pilot-link first (see palm-client/README.md)." >&2
	exit 1
fi

echo "Press the HotSync button on the Visor cradle..."

if [[ "$port" == /dev/* ]]; then
	# The visor driver only creates the tty once HotSync starts.
	for _ in $(seq 60); do
		[[ -e "$port" ]] && break
		sleep 1
	done
	if [[ ! -e "$port" ]]; then
		echo "Timed out waiting for $port. Is the visor kernel module loaded? (lsmod | grep visor)" >&2
		exit 1
	fi
fi

pilot-xfer -p "$port" -i "$prc"
