"""Writes what JLCPCB needs to quote and build the board: a Gerber zip, a BOM and a placement file.

  export_jlcpcb.py <board.kicad_pcb> <output directory>

Any Python 3; calls KiCad 10's kicad-cli. Only the surface-mount parts JLCPCB is to place are in
the BOM and the placement file. Left out, for fitting by hand: the two isolators (the owner has
them in stock; JLCPCB had none of the narrow package), the four DB9 sockets, the four headers and the RP2040-Zero.
"""
import csv
import os
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

KICAD_CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
NAME = "rp2040-encoder-carrier-r2"

# (kind, value) -> (JLCPCB/LCSC part number, description for the BOM's comment column).
# All are "Basic" parts of JLCPCB's assembly library, checked in stock on 2026-11-22.
JLC_PARTS = {
    ("r", "1k"): ("C21190", "1k 1% 0603"),
    ("r", "0R"): ("C21189", "0R 0603"),
    ("r", "10R"): ("C22859", "10R 1% 0603"),
    ("r", "2k2"): ("C4190", "2.2k 1% 0603"),
    ("c", "100p"): ("C14858", "100pF 50V C0G 0603"),
    ("c", "100n"): ("C14663", "100nF 50V X7R 0603"),
    ("cbulk", "10u"): ("C15850", "10uF 25V X5R 0805"),
    ("cbulk", "1u"): ("C28323", "1uF 50V X7R 0805"),
    ("led", "5V"): ("C2286", "LED red 0603"),
}
JLC_FOOTPRINT = {"r": "0603", "c": "0603", "cbulk": "0805", "led": "LED_0603"}
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"


def run(*args):
    result = subprocess.run([KICAD_CLI] + list(args), capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("kicad-cli %s failed:\n%s%s" % (" ".join(args[:3]), result.stdout, result.stderr))


def number(ref):
    return int("".join(ch for ch in ref if ch.isdigit()))


def main(board, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    parts, _ = D.build()
    placed = {p["ref"]: p for p in parts if (p["kind"], p["value"]) in JLC_PARTS}

    # --- Gerbers and drill files, zipped
    with tempfile.TemporaryDirectory() as tmp:
        run("pcb", "export", "gerbers", "--output", tmp + os.sep, "--layers", LAYERS, "--subtract-soldermask", board)
        run("pcb", "export", "drill", "--output", tmp + os.sep, "--format", "excellon", "--excellon-units", "mm",
            "--excellon-separate-th", "--drill-origin", "absolute", board)
        files = sorted(f for f in os.listdir(tmp) if not f.endswith(".gbrjob"))
        zip_path = os.path.join(out_dir, NAME + "-gerbers.zip")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for f in files:
                z.write(os.path.join(tmp, f), f)
        # --- placement, from KiCad's own position file
        pos_path = os.path.join(tmp, "pos.csv")
        run("pcb", "export", "pos", "--output", pos_path, "--format", "csv", "--units", "mm", "--side", "front",
            "--smd-only", board)
        rows = list(csv.DictReader(open(pos_path, newline="")))
    cpl_path = os.path.join(out_dir, NAME + "-cpl-jlcpcb.csv")
    count = 0
    with open(cpl_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in sorted(rows, key=lambda r: (r["Ref"].rstrip("0123456789"), number(r["Ref"]))):
            if row["Ref"] in placed:
                w.writerow([row["Ref"], "%.4fmm" % float(row["PosX"]), "%.4fmm" % float(row["PosY"]), "Top",
                            "%g" % (float(row["Rot"]) % 360)])
                count += 1
    if count != len(placed):
        raise SystemExit("placement file has %d of %d parts" % (count, len(placed)))

    # --- BOM, one line per part number
    groups = {}
    for ref, part in placed.items():
        groups.setdefault((part["kind"], part["value"]), []).append(ref)
    bom_path = os.path.join(out_dir, NAME + "-bom-jlcpcb.csv")
    with open(bom_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for key in sorted(groups, key=lambda k: (k[0], k[1])):
            lcsc, comment = JLC_PARTS[key]
            w.writerow([comment, ",".join(sorted(groups[key], key=number)), JLC_FOOTPRINT[key[0]], lcsc])
    left_out = sorted((p["ref"] for p in parts if p["ref"] not in placed and p["kind"] != "hole"),
                      key=lambda r: (r.rstrip("0123456789"), number(r)))
    print("gerbers: %s (%d files)" % (os.path.basename(zip_path), len(files)))
    for f in files:
        print("   ", f)
    print("bom: %s, %d lines, %d parts" % (os.path.basename(bom_path), len(groups), len(placed)))
    print("cpl: %s, %d placements" % (os.path.basename(cpl_path), count))
    print("not placed by JLCPCB:", " ".join(left_out))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
