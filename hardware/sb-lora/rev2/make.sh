#!/bin/bash
# Rev 2 pipeline: place -> project -> autoroute -> import -> pour -> DRC -> verify.
# Needs KiCad 10 (its Python and kicad-cli), Java 21+ and freerouting.jar.
#   KICAD_APP  default: the Homebrew cask's KiCad.app
#   FREEROUTING  path to freerouting.jar (v1.9.0; 2.x ignores the pass limit)
#   JAVA       default: Homebrew openjdk@21
set -e
cd "$(dirname "$0")"
KICAD_APP=${KICAD_APP:-/opt/homebrew/Caskroom/kicad/10.0.8/KiCad/KiCad.app}
PY=$(ls -d "$KICAD_APP"/Contents/Frameworks/Python.framework/Versions/3*/bin/python3 | head -1)
CLI="$KICAD_APP/Contents/MacOS/kicad-cli"
JAVA=${JAVA:-/opt/homebrew/opt/openjdk@21/bin/java}
: "${FREEROUTING:?set FREEROUTING to freerouting.jar}"
OUT=${OUT:-kicad}
mkdir -p "$OUT"
PCB="$OUT/visormesh.kicad_pcb"
"$PY" set_project.py "$OUT/visormesh.kicad_pro"
"$PY" build_board.py "$PCB"
cp "$PCB" "$OUT/visormesh_unrouted.kicad_pcb"
"$PY" -c "import pcbnew,sys; b=pcbnew.LoadBoard('$PCB'); pcbnew.ExportSpecctraDSN(b,'$OUT/visormesh.dsn')"
# -oit 1: stop optimising below 1 % improvement (the optimiser can otherwise run for ever).
# Watchdog: give up after 5 minutes rather than hang.
rm -f "$OUT/visormesh.ses"
"$JAVA" -jar "$FREEROUTING" -de "$OUT/visormesh.dsn" -do "$OUT/visormesh.ses" -mp ${PASSES:-100} -oit 1 \
    >"$OUT/freerouting.log" 2>&1 &
FR=$!
for _ in $(seq 300); do kill -0 $FR 2>/dev/null || break; sleep 1; done
if kill -0 $FR 2>/dev/null; then kill $FR; echo "freerouting timed out" >&2; exit 1; fi
[ -s "$OUT/visormesh.ses" ] || { echo "freerouting wrote no session" >&2; exit 1; }
"$PY" finish_board.py "$PCB" "$OUT/visormesh.ses"
"$CLI" pcb drc --format report -o "$OUT/drc.rpt" "$PCB" | tail -1
grep -E "^\[" "$OUT/drc.rpt" | sort | uniq -c | sort -rn
"$PY" verify.py "$PCB"
