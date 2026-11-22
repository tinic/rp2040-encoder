#!/bin/sh
# Checks the committed schematic and board and writes the fabrication files.
#
#   gen/check.sh <output directory>
#
# Needs KiCad 10's kicad-cli and python3 on the PATH. Used by CI; the routing itself is not
# redone here (gen/build.ps1 does that).
set -eu
out=${1:?output directory}
cd "$(dirname "$0")/.."
mkdir -p "$out"
name=rp2040-encoder-carrier

# schematic: electrical rules, and the same nets as gen/design.py
kicad-cli sch erc --severity-error --exit-code-violations --format report \
    --output "$out/erc.rpt" "$name.kicad_sch"
kicad-cli sch export netlist --format kicadsexpr --output "$out/$name.net" "$name.kicad_sch"
python3 gen/check_netlist.py "$out/$name.net"

# board: design rules, nothing unconnected, and it matches the schematic
kicad-cli pcb drc --schematic-parity --severity-error --exit-code-violations --format report \
    --output "$out/drc.rpt" "$name.kicad_pcb"

python3 gen/export_jlcpcb.py "$name.kicad_pcb" "$out"
rm -f "$out/$name.net"
