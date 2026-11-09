#!/bin/sh
# Start, stop or check visor_bridge.py in the background.
#   tools/bridge.sh start [port]   default port: the first /dev/cu.usbserial-*
#   tools/bridge.sh stop | restart [port] | status | log
# The log goes to $TMPDIR/visor_bridge.log, and every frame in full to
# $TMPDIR/visor_frames.txt for `make replay`.

cd "$(dirname "$0")/.." || exit 1
LOG="${TMPDIR:-/tmp}/visor_bridge.log"
FRAMES="${TMPDIR:-/tmp}/visor_frames.txt"
PATTERN=tools/visor_bridge.py

running() { pgrep -f "$PATTERN" >/dev/null; }

start() {
	if running; then echo "Bridge already running (pid $(pgrep -f "$PATTERN" | tr "\n" " "))"; return 0; fi
	port="$1"
	[ -n "$port" ] || port=$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)
	[ -n "$port" ] || { echo "No /dev/cu.usbserial-* found; pass the port"; return 1; }
	nohup python3 "$PATTERN" --capture "$FRAMES" "$port" >"$LOG" 2>&1 &
	sleep 1
	if running; then
		echo "Bridge started on $port (log: $LOG)"
	else
		echo "Bridge exited at startup:"; tail -5 "$LOG"; return 1
	fi
}

stop() {
	if running; then pkill -f "$PATTERN"; sleep 1; echo "Bridge stopped"
	else echo "Bridge not running"; fi
}

case "$1" in
	start)   start "$2" ;;
	stop)    stop ;;
	restart) stop; start "$2" ;;
	status)  if running; then echo "Running (pid $(pgrep -f "$PATTERN" | tr "\n" " "))"; tail -3 "$LOG" 2>/dev/null; true
	         else echo "Not running"; fi ;;
	log)     tail -f "$LOG" ;;
	*)       echo "usage: $0 start [port] | stop | restart [port] | status | log"; exit 1 ;;
esac
