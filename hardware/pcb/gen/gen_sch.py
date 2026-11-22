"""Writes the carrier board's schematic from design.py.

  gen_sch.py <out.kicad_sch>

Any Python 3. Symbols are copied from KiCad 10's libraries and from the project library
lib/rp2040_encoder.kicad_sym, which this script also writes. Connections are drawn with net
labels on short stubs; the input filters are drawn with wires. After writing, compare the
netlist KiCad exports from the schematic with design.py (check_netlist.py does that).
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

KICAD_SYM = r"C:\Program Files\KiCad\10.0\share\kicad\symbols"
HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = "rp2040-encoder-carrier"
ROOT_UUID = D.uid("root sheet")
FONT = "(effects (font (size 1.27 1.27)))"
HIDDEN = "(effects (font (size 1.27 1.27)) (hide yes))"

SYMBOL = {  # kind -> (library, symbol)
    "idc": ("Connector_Generic", "Conn_02x05_Odd_Even"),
    "iso": ("Isolator", "ISO7760FDBQ"),
    "r": ("Device", "R"),
    "c": ("Device", "C"),
    "cbulk": ("Device", "C"),
    "led": ("Device", "LED"),
    "hole": ("Mechanical", "MountingHole"),
    "zero": ("rp2040_encoder", "RP2040-Zero"),
    "flag": ("power", "PWR_FLAG"),
}


def zero_symbol():
    """The RP2040-Zero as a symbol: power and R inputs on the left, A/B inputs on the right."""
    left = [(23, 12.7), (22, 10.16), (21, 7.62), (20, 2.54), (19, 0.0), (18, -2.54), (17, -5.08),
            (16, -10.16), (15, -12.7)]
    right = [(n, 12.7 - 2.54 * (n - 1)) for n in range(1, 10)] + [(n, -12.7 - 2.54 * (n - 10)) for n in range(10, 15)]
    kinds = {"5V": "power_out", "3V3": "power_out", "GND": "power_in"}
    pins = []
    for side, x, angle in ((left, -12.7, 0), (right, 12.7, 180)):
        for number, y in side:
            name = D.ZERO_PINS[number]
            pins.append('      (pin %s line (at %s %s %d) (length 2.54) (name "%s" %s) (number "%d" %s))' % (
                kinds.get(name, "bidirectional"), x, y, angle, name, FONT, number, FONT))
    return "\n".join([
        '  (symbol "RP2040-Zero" (exclude_from_sim no) (in_bom yes) (on_board yes)',
        '    (property "Reference" "U" (at -10.16 16.51 0) %s)' % FONT,
        '    (property "Value" "RP2040-Zero" (at 0 -26.67 0) %s)' % FONT,
        '    (property "Footprint" "rp2040_encoder:RP2040-Zero" (at 0 0 0) %s)' % HIDDEN,
        '    (property "Datasheet" "https://www.waveshare.com/wiki/RP2040-Zero" (at 0 0 0) %s)' % HIDDEN,
        '    (property "Description" "Waveshare RP2040-Zero module, pin numbers as Waveshare" (at 0 0 0) %s)' % HIDDEN,
        '    (symbol "RP2040-Zero_0_1"',
        '      (rectangle (start -10.16 15.24) (end 10.16 -25.4) (stroke (width 0.254) (type default)) (fill (type background))))',
        '    (symbol "RP2040-Zero_1_1"',
    ] + pins + ['    )', '  )'])


def balanced(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise ValueError("unbalanced")


def elements(symbol_text):
    """The top-level lists inside one (symbol ...) text."""
    out, i = [], symbol_text.index("(", 1)
    while i != -1 and i < len(symbol_text) - 1:
        item = balanced(symbol_text, i)
        out.append(item)
        i = symbol_text.find("(", i + len(item))
    return out


_lib_cache = {}


def library_symbol(lib, name):
    """The symbol's text as it goes into lib_symbols, derived symbols flattened."""
    if lib == "rp2040_encoder":
        body = zero_symbol().strip()
    else:
        if lib not in _lib_cache:
            _lib_cache[lib] = open(os.path.join(KICAD_SYM, lib + ".kicad_sym"), encoding="utf-8").read()
        text = _lib_cache[lib]
        body = balanced(text, text.index('(symbol "%s"' % name))
        parent = re.search(r'\(extends "([^"]+)"\)', body)
        if parent:
            # A derived symbol has its own properties and takes everything else from its parent.
            base = balanced(text, text.index('(symbol "%s"' % parent.group(1)))
            own = {re.match(r'\(property "([^"]+)"', e).group(1): e for e in elements(body) if e.startswith("(property")}
            merged = []
            for element in elements(base):
                if element.startswith("(property"):
                    key = re.match(r'\(property "([^"]+)"', element).group(1)
                    element = own.pop(key, element)
                merged.append(element.replace('(symbol "%s_' % parent.group(1), '(symbol "%s_' % name))
            first_unit = next(i for i, e in enumerate(merged) if e.startswith("(symbol"))
            merged[first_unit:first_unit] = list(own.values())
            body = '(symbol "%s"\n  %s\n)' % (name, "\n  ".join(merged))
    return body.replace('(symbol "%s"' % name, '(symbol "%s:%s"' % (lib, name), 1)


def pins_of(symbol_text):
    found = re.findall(r'\(pin\s+\w+\s+\w+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)\s*\(length\s+[-\d.]+\)'
                       r'(?:.|\n)*?\(number\s+"([^"]*)"', symbol_text)
    return {number: (float(x), float(y), float(a)) for x, y, a, number in found}


def fmt(v):
    s = "%.3f" % v
    return s.rstrip("0").rstrip(".") if "." in s else s


class Sheet:
    def __init__(self):
        self.items = []
        self.libs = {}
        self.count = 0

    def uid(self, what):
        self.count += 1
        return D.uid("%s #%d" % (what, self.count))

    def wire(self, x1, y1, x2, y2):
        self.items.append('  (wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid "%s"))' % (
            fmt(x1), fmt(y1), fmt(x2), fmt(y2), self.uid("wire")))

    def junction(self, x, y):
        self.items.append('  (junction (at %s %s) (diameter 0) (color 0 0 0 0) (uuid "%s"))' % (fmt(x), fmt(y), self.uid("junction")))

    def label(self, net, x, y, dx, dy):
        """A net label at (x, y) whose text runs in the direction (dx, dy)."""
        angle, justify = {(1, 0): (0, "left"), (-1, 0): (180, "right"), (0, -1): (90, "left"), (0, 1): (270, "right")}[(dx, dy)]
        self.items.append('  (label "%s" (at %s %s %d) (effects (font (size 1.27 1.27)) (justify %s bottom)) (uuid "%s"))' % (
            net, fmt(x), fmt(y), angle, justify, self.uid("label")))

    def note(self, s, x, y, size=2.0):
        self.items.append('  (text "%s" (exclude_from_sim no) (at %s %s 0) (effects (font (size %s %s)) (justify left bottom)) (uuid "%s"))' % (
            s, fmt(x), fmt(y), fmt(size), fmt(size), self.uid("text")))

    def no_connect(self, x, y):
        self.items.append('  (no_connect (at %s %s) (uuid "%s"))' % (fmt(x), fmt(y), self.uid("nc")))

    def symbol(self, kind, ref, value, footprint, x, y, rot, nets, ref_at, value_at, wired=()):
        """Places a symbol. `nets` maps pin number to a net (labelled on a stub) or None (no connect).
        Pins listed in `wired` get neither; the caller draws their wires. Returns pin positions."""
        lib, name = SYMBOL[kind]
        key = "%s:%s" % (lib, name)
        if key not in self.libs:
            self.libs[key] = library_symbol(lib, name)
        pins = pins_of(self.libs[key])
        at = {}
        rad = math.radians(rot)
        lines = ['  (symbol (lib_id "%s") (at %s %s %d) (unit 1) (exclude_from_sim no) (in_bom %s) (on_board yes) (dnp no) (uuid "%s")' % (
            key, fmt(x), fmt(y), rot, "no" if kind in ("flag", "hole") else "yes", D.uid("symbol " + ref))]
        show = HIDDEN if kind == "flag" else FONT
        lines.append('    (property "Reference" "%s" (at %s %s 0) %s)' % (ref, fmt(x + ref_at[0]), fmt(y + ref_at[1]), show))
        lines.append('    (property "Value" "%s" (at %s %s 0) %s)' % (value, fmt(x + value_at[0]), fmt(y + value_at[1]), FONT))
        lines.append('    (property "Footprint" "%s" (at %s %s 0) %s)' % (footprint, fmt(x), fmt(y), HIDDEN))
        lines.append('    (property "Datasheet" "" (at %s %s 0) %s)' % (fmt(x), fmt(y), HIDDEN))
        lines.append('    (property "Description" "" (at %s %s 0) %s)' % (fmt(x), fmt(y), HIDDEN))
        for number in sorted(pins, key=lambda n: (len(n), n)):
            lines.append('    (pin "%s" (uuid "%s"))' % (number, D.uid("pin %s.%s" % (ref, number))))
        lines.append('    (instances (project "%s" (path "/%s" (reference "%s") (unit 1))))' % (PROJECT, ROOT_UUID, ref))
        lines.append("  )")
        self.items.append("\n".join(lines))
        for number, (px, py, pa) in pins.items():
            # symbol coordinates have y up; rotate counter-clockwise, then flip y for the sheet
            sx = x + px * math.cos(rad) - py * math.sin(rad)
            sy = y - (px * math.sin(rad) + py * math.cos(rad))
            # the pin's line runs from its connection point towards the body
            bx = round(math.cos(math.radians(pa + rot)))
            by = -round(math.sin(math.radians(pa + rot)))
            at[number] = (round(sx, 3), round(sy, 3), -bx, -by)   # with the outward direction
            if number in wired:
                continue
            if number not in nets:
                raise SystemExit("%s pin %s has no net and is not wired" % (ref, number))
            net = nets[number]
            if net is None:
                self.no_connect(sx, sy)
            else:
                ex, ey = sx - bx * 2.54, sy - by * 2.54
                self.wire(sx, sy, ex, ey)
                self.label(net, ex, ey, -bx, -by)
        return at

    def text(self, title):
        out = ['(kicad_sch (version 20250114) (generator "rp2040_encoder_gen") (generator_version "9.0")',
               '  (uuid "%s")' % ROOT_UUID,
               '  (paper "A3")',
               '  (title_block (title "%s") (rev "r1") (company "tinic/rp2040-encoder")' % title,
               '    (comment 1 "Generated by hardware/pcb/gen/gen_sch.py from design.py"))',
               '  (lib_symbols']
        for key in sorted(self.libs):
            out.append("    " + self.libs[key].replace("\n", "\n    "))
        out.append("  )")
        out.extend(self.items)
        out.append('  (sheet_instances (path "/" (page "1")))')
        out.append('  (embedded_fonts no)')
        out.append(")")
        return "\n".join(out) + "\n"


def footprint_of(kind):
    if kind == "flag":
        return ""
    return "%s:%s" % D.FP[kind]


def main(out_path):
    parts, nets = D.build()
    by_ref = {p["ref"]: p for p in parts}
    pin_nets = {}
    for (ref, pad), net in nets.items():
        pin_nets.setdefault(ref, {})[pad] = net
    sheet = Sheet()

    def put(ref, x, y, rot=0, ref_at=(0, -5.08), value_at=(0, 5.08), wired=(), extra=None):
        part = by_ref[ref]
        mapping = dict(pin_nets.get(ref, {}))
        if extra:
            mapping.update(extra)
        return sheet.symbol(part["kind"], ref, part["value"], footprint_of(part["kind"]), x, y, rot, mapping,
                            ref_at, value_at, wired)

    # ---- pass-through connectors: one column per axis, scale connector above the DRO connector
    sheet.note("Scale and DRO connectors: 2x5 headers for DB9 ribbon cables, every pin passed through", 25.4, 25.4, 2.5)
    sheet.note("Header pin 1..9 = DB9 pin 1, 6, 2, 7, 3, 8, 4, 9, 5. Net <axis>_D<n> is DB9 pin n; DB9 pin 2 (0 V) is GND1.", 25.4, 30.48, 1.6)
    for i, axis in enumerate(D.AXES):
        x = 45.72 + i * 48.26
        for row, y in enumerate((53.34, 86.36)):
            ref = "J%d" % (2 * i + 1 + row)
            put(ref, x, y, ref_at=(1.27, -8.89), value_at=(1.27, 8.89), extra={"10": None})
        sheet.note("%s axis" % axis, x - 7.62, 40.64, 2.0)

    # ---- input filters: tap - series resistor - node (to the isolator), capacitor to ground
    sheet.note("Input filters: 1k in series, 100p to ground, at each isolator input", 25.4, 111.76, 2.5)
    filters = [(ref, ch) for ref in ("U1", "U2") for ch in range(6)]
    for k, (iso, ch) in enumerate(filters):
        n = k + 1 if iso == "U1" else k + 1            # R1..R12 in channel order
        r_ref = "R%d" % n
        c_ref = "C%d" % (ch + 1 if iso == "U1" else ch + 9)
        x0 = 40.64 + (k % 4) * 48.26
        y0 = 127.0 + (k // 4) * 22.86
        r_pins = put(r_ref, x0, y0, rot=90, ref_at=(0, -2.54), value_at=(0, 2.54), wired=("2",))
        c_pins = put(c_ref, x0 + 8.89, y0 + 3.81, ref_at=(3.81, -1.27), value_at=(3.81, 1.27), wired=("1",))
        node = pin_nets[r_ref]["2"]
        rx, ry = r_pins["2"][0], r_pins["2"][1]
        cx, cy = c_pins["1"][0], c_pins["1"][1]
        assert abs(cy - ry) < 1e-6 and cx > rx, (r_ref, r_pins, c_pins)
        sheet.wire(rx, ry, cx + 5.08, ry)
        sheet.junction(cx, cy)
        sheet.label(node, cx + 5.08, ry, 1, 0)

    # ---- isolators
    sheet.note("Isolators", 236.22, 25.4, 2.5)
    sheet.note("Machine side (VCC1, GND1) left, USB side (+3V3, GND2) right", 236.22, 30.48, 1.6)
    put("U1", 261.62, 60.96, ref_at=(-7.62, -20.32), value_at=(8.89, -20.32))
    put("U2", 261.62, 119.38, ref_at=(-7.62, -20.32), value_at=(8.89, -20.32))

    # ---- decoupling and bulk capacitors
    sheet.note("Decoupling", 236.22, 152.4, 2.5)
    for k, ref in enumerate(("C7", "C15", "C17", "C8", "C16", "C18")):
        put(ref, 243.84 + k * 15.24, 167.64, ref_at=(3.81, -1.27), value_at=(3.81, 1.27))

    # ---- controller
    sheet.note("RP2040-Zero", 330.2, 25.4, 2.5)
    sheet.note("A/B on GP0-GP7, R on GP26-GP29, as the firmware expects", 330.2, 30.48, 1.6)
    unused = {str(pin): None for pin in D.ZERO_PINS if str(pin) not in pin_nets["U3"]}
    put("U3", 355.6, 63.5, ref_at=(-7.62, -17.78), value_at=(0, 27.94), extra=unused)

    # ---- machine-side supply
    sheet.note("Machine-side supply", 320.04, 106.68, 2.5)
    sheet.note("+5 V of each scale connector, joined through 0R links", 320.04, 111.76, 1.6)
    for i in range(4):
        put("R%d" % (15 + i), 345.44, 124.46 + i * 10.16, rot=90, ref_at=(0, -2.54), value_at=(0, 2.54))
    put("R13", 345.44, 167.64, rot=90, ref_at=(0, -2.54), value_at=(0, 2.54))
    put("R14", 345.44, 180.34, rot=90, ref_at=(0, -2.54), value_at=(0, 2.54))
    put("D1", 345.44, 193.04, ref_at=(0, -2.54), value_at=(0, 2.54))

    # ---- power flags for the electrical rules check, and the mounting holes
    for k, net in enumerate(("VCC1", "GND1", "GND2")):
        x = 320.04 + k * 15.24
        sheet.symbol("flag", "#FLG0%d" % (k + 1), "PWR_FLAG", "", x, 220.98, 0, {"1": net}, (0, -2.54), (0, -3.81))
    for k, ref in enumerate(("H1", "H2")):
        put(ref, 320.04 + k * 15.24, 246.38, ref_at=(0, -3.81), value_at=(0, 3.81))

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(sheet.text("RP2040-Zero carrier for four glass scales"))
    lib_path = os.path.join(os.path.dirname(HERE), "lib", "rp2040_encoder.kicad_sym")
    with open(lib_path, "w", encoding="utf-8", newline="\n") as f:
        f.write('(kicad_symbol_lib (version 20241209) (generator "rp2040_encoder_gen") (generator_version "9.0")\n%s\n)\n' % zero_symbol())
    table = os.path.join(os.path.dirname(HERE), "sym-lib-table")
    with open(table, "w", encoding="utf-8", newline="\n") as f:
        f.write('(sym_lib_table\n  (version 7)\n  (lib (name "rp2040_encoder")(type "KiCad")(uri "${KIPRJMOD}/lib/rp2040_encoder.kicad_sym")'
                '(options "")(descr "Parts of the rp2040-encoder carrier board"))\n)\n')
    print("wrote %s: %d items, %d library symbols" % (os.path.basename(out_path), len(sheet.items), len(sheet.libs)))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
