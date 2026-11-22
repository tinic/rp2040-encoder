"""Compares a netlist exported by KiCad from the schematic with design.py.

  kicad-cli sch export netlist --format kicadsexpr -o carrier.net rp2040-encoder-carrier.kicad_sch
  check_netlist.py carrier.net
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402


def main(path):
    text = open(path, encoding="utf-8").read()
    found = {}
    for block in re.split(r"\(net\s*\(code", text)[1:]:
        name = re.search(r'\(name "([^"]*)"\)', block).group(1).lstrip("/")
        for ref, pin in re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', block):
            if not ref.startswith("#"):
                found[(ref, pin)] = name
    _, wanted = D.build()
    problems = []
    for key in sorted(set(found) | set(wanted)):
        have, want = found.get(key), wanted.get(key)
        if want is None and have is not None and have.startswith("unconnected-"):
            continue
        if have != want:
            problems.append("%s.%s: schematic %s, design %s" % (key[0], key[1], have, want))
    print("%d pins in the schematic netlist, %d in the design, %d differences" % (len(found), len(wanted), len(problems)))
    for line in problems[:40]:
        print("  " + line)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
