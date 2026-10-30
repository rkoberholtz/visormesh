#!/bin/bash
# Full pipeline: place -> autoroute -> import -> pour -> DRC
set -e
cd "$(dirname "$0")"
/usr/bin/python3 build_board.py visormesh.kicad_pcb
python3 set_project.py visormesh.kicad_pro
cp visormesh.kicad_pcb visormesh_unrouted.kicad_pcb
/usr/bin/python3 -c "import pcbnew; b=pcbnew.LoadBoard('visormesh.kicad_pcb'); pcbnew.ExportSpecctraDSN(b,'visormesh.dsn')"
java -jar ${FREEROUTING:-/home/claude/tools/freerouting.jar} -de visormesh.dsn -do visormesh.ses -mp 30 -host-mode cli >freerouting.log 2>&1
/usr/bin/python3 finish_board.py visormesh.kicad_pcb visormesh.ses
/usr/bin/python3 run_drc.py visormesh.kicad_pcb drc.rpt
