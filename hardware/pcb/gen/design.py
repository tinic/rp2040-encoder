"""The carrier board as data: parts, nets and placement.

One source for the schematic generator (gen_sch.py) and the board generator (gen_pcb.py).
Lengths are millimetres; the board origin is its top left corner, x to the right, y down.

The board passes four scale connectors (DB9 on a ribbon to a 2x5 header) straight through
to four DRO connectors, and taps A, B, R, +5 V and 0 V of each. The taps go through two
ISO7760F isolators to an RP2040-Zero.
"""

import uuid

AXES = ["X", "Y", "Z", "A"]

_UUID_NAMESPACE = uuid.UUID("6f1d2c0e-8a47-4e0b-9a35-2f0d5b7c1e10")


def uid(name):
    """A stable identifier for a named thing, so that regenerating keeps schematic and board linked."""
    return str(uuid.uuid5(_UUID_NAMESPACE, name))


# 2x5 header pin -> DB9 pin for a ribbon crimped DB9 (wire n is header pin n).
HEADER_TO_DB9 = {1: 1, 2: 6, 3: 2, 4: 7, 5: 3, 6: 8, 7: 4, 8: 9, 9: 5, 10: None}

# DB9 pins tapped. +5 V, 0 V, A and B are the "generic Chinese" glass scale pinout (ToAuto and
# others). On the owner's scales pin 5 is not wired and the reference pulse is on pin 9; their
# cables also join pin 3 with 6 and pin 4 with 8, which the board passes through unchanged.
DB9_5V, DB9_0V, DB9_A, DB9_B, DB9_R = 1, 2, 3, 4, 9

BOARD_W, BOARD_H = 143.0, 25.0

HEADER_X = [18.0, 39.6, 61.2, 82.8]      # centre of each axis' header pair
HEADER_Y_IN, HEADER_Y_OUT = 6.3, 18.7    # scale row on top, DRO row at the bottom

ISO_X = 113.0                            # centre line of the isolation barrier
ISO_Y = {"U2": 6.5, "U1": 18.5}
BARRIER_HALF = 1.6                       # no copper within this distance of ISO_X

ZERO_X, ZERO_Y = 131.25, 12.5

FP = {
    "idc": ("Connector_IDC", "IDC-Header_2x05_P2.54mm_Vertical"),
    "iso": ("Package_SO", "QSOP-16_3.9x4.9mm_P0.635mm"),
    "r": ("Resistor_SMD", "R_0603_1608Metric"),
    "c": ("Capacitor_SMD", "C_0603_1608Metric"),
    "cbulk": ("Capacitor_SMD", "C_0805_2012Metric"),
    "led": ("LED_SMD", "LED_0603_1608Metric"),
    "hole": ("MountingHole", "MountingHole_3.2mm_M3"),
    "zero": ("rp2040_encoder", "RP2040-Zero"),
}

# RP2040-Zero pin number -> name, Waveshare's numbering (top view, USB up: 1 is top right).
ZERO_PINS = {1: "GP0", 2: "GP1", 3: "GP2", 4: "GP3", 5: "GP4", 6: "GP5", 7: "GP6", 8: "GP7",
             9: "GP8", 10: "GP9", 11: "GP10", 12: "GP11", 13: "GP12", 14: "GP13", 15: "GP14",
             16: "GP15", 17: "GP26", 18: "GP27", 19: "GP28", 20: "GP29", 21: "3V3", 22: "GND",
             23: "5V"}

# Isolator channel -> (tapped signal, RP2040 GPIO). The firmware fixes the GPIOs:
# A/B of X, Y, Z, A on GP0..GP7, R of X, Y, Z, A on GP26..GP29.
ISO_CHANNELS = {
    "U1": [("X_A", "GP0"), ("X_B", "GP1"), ("Y_A", "GP2"), ("Y_B", "GP3"), ("Z_A", "GP4"), ("Z_B", "GP5")],
    "U2": [("A_A", "GP6"), ("A_B", "GP7"), ("X_R", "GP26"), ("Y_R", "GP27"), ("Z_R", "GP28"), ("A_R", "GP29")],
}


def db9_net(axis, pin):
    """Net of a DB9 pin of one axis. 0 V of every axis is the machine-side ground."""
    return "GND1" if pin == DB9_0V else "%s_D%d" % (axis, pin)


def tap_net(signal):
    axis, kind = signal.split("_")
    return db9_net(axis, {"A": DB9_A, "B": DB9_B, "R": DB9_R}[kind])


def build():
    """Returns (parts, nets): parts is a list of dicts, nets maps (ref, pad) -> net name."""
    parts, nets = [], {}

    def add(ref, kind, value, x, y, rot=0.0, **extra):
        parts.append(dict(ref=ref, kind=kind, value=value, x=x, y=y, rot=rot, **extra))

    # --- pass-through connectors
    for i, axis in enumerate(AXES):
        for row, (suffix, y, rot) in enumerate((("SCALE", HEADER_Y_IN, 90.0), ("DRO", HEADER_Y_OUT, -90.0))):
            ref = "J%d" % (2 * i + 1 + row)
            add(ref, "idc", "%s_%s" % (axis, suffix), HEADER_X[i], y, rot, centre=True, axis=axis, role=suffix)
            for pin, db9 in HEADER_TO_DB9.items():
                if db9 is not None:
                    nets[(ref, str(pin))] = db9_net(axis, db9)

    # --- isolators with their input filters (series R, C to ground) and decoupling
    r_n = c_n = 0
    for ref in ("U1", "U2"):
        y0 = ISO_Y[ref]
        add(ref, "iso", "ISO7760FDBQR", ISO_X, y0)
        nets[(ref, "1")] = "VCC1"
        nets[(ref, "8")] = "GND1"
        nets[(ref, "9")] = "GND2"
        nets[(ref, "16")] = "+3V3"
        for ch, (signal, gpio) in enumerate(ISO_CHANNELS[ref]):
            node = signal + "_F"                      # filtered tap at the isolator input
            nets[(ref, str(2 + ch))] = node           # INA..INF are pins 2..7
            nets[(ref, str(15 - ch))] = gpio          # OUTA..OUTF are pins 15..10
            row_y = y0 + (ch - 2.5) * 1.7
            r_n += 1
            c_n += 1
            add("R%d" % r_n, "r", "1k", ISO_X - 10.0, row_y)
            nets[("R%d" % r_n, "1")] = tap_net(signal)
            nets[("R%d" % r_n, "2")] = node
            add("C%d" % c_n, "c", "100p", ISO_X - 6.7, row_y)
            nets[("C%d" % c_n, "1")] = node
            nets[("C%d" % c_n, "2")] = "GND1"
        # decoupling: machine side left of the barrier, logic side right of it
        dec_y = y0 - 4.5 if ref == "U2" else y0 + 4.5
        c_n += 1
        add("C%d" % c_n, "c", "100n", ISO_X - 3.4, dec_y, 180.0)
        nets[("C%d" % c_n, "1")] = "VCC1"
        nets[("C%d" % c_n, "2")] = "GND1"
        c_n += 1
        add("C%d" % c_n, "c", "100n", ISO_X + 3.4, dec_y)
        nets[("C%d" % c_n, "1")] = "+3V3"
        nets[("C%d" % c_n, "2")] = "GND2"

    # bulk capacitors between the two isolators
    add("C17", "cbulk", "10u", ISO_X - 3.7, 12.5, 90.0)
    nets[("C17", "1")] = "VCC1"
    nets[("C17", "2")] = "GND1"
    add("C18", "cbulk", "1u", ISO_X + 3.7, 12.5, 90.0)
    nets[("C18", "1")] = "+3V3"
    nets[("C18", "2")] = "GND2"

    # --- machine-side supply: the +5 V of each scale connector, joined through 0R links.
    # All four come from the one DRO supply. Links, not diodes: the isolators must run from the
    # rail that drives their inputs (ISO7760: an input may not exceed VCC1). Leave a link out to
    # keep one connector's +5 V apart.
    px = 97.3
    for i, axis in enumerate(AXES):
        ref = "R%d" % (15 + i)
        add(ref, "r", "0R", px - 1.7 + 3.4 * (i % 2), 10.6 + 4.0 * (i // 2), 90.0)
        nets[(ref, "1")] = db9_net(axis, DB9_5V)
        nets[(ref, "2")] = "VCC1_RAW"
    add("R13", "r", "10R", px, 19.4)
    nets[("R13", "1")] = "VCC1_RAW"
    nets[("R13", "2")] = "VCC1"
    add("R14", "r", "2k2", px, 21.2)
    nets[("R14", "1")] = "VCC1"
    nets[("R14", "2")] = "LED_A"
    add("D1", "led", "5V", px, 23.0)
    nets[("D1", "2")] = "LED_A"                    # anode
    nets[("D1", "1")] = "GND1"                     # cathode

    # --- the controller
    add("U3", "zero", "RP2040-Zero", ZERO_X, ZERO_Y, -90.0)
    used = {gpio for chans in ISO_CHANNELS.values() for _, gpio in chans}
    for pin, name in ZERO_PINS.items():
        if name in used:
            nets[("U3", str(pin))] = name          # the other GPIOs and 5V stay unconnected
    nets[("U3", "21")] = "+3V3"
    nets[("U3", "22")] = "GND2"

    # --- mounting holes, both on the machine side
    add("H1", "hole", "M3", 3.6, 12.5)
    add("H2", "hole", "M3", px, 3.7)
    return parts, nets


if __name__ == "__main__":
    parts, nets = build()
    print(len(parts), "parts,", len(set(nets.values())), "nets")
    by_net = {}
    for (ref, pad), net in sorted(nets.items()):
        by_net.setdefault(net, []).append("%s.%s" % (ref, pad))
    for net in sorted(by_net):
        print("%-10s %s" % (net, " ".join(by_net[net])))
