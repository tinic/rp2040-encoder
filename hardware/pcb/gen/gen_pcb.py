"""Builds the carrier board with KiCad's pcbnew module, from design.py.

  gen_pcb.py place  <board.kicad_pcb> <out.dsn>   parts placed, nets assigned, outline, rule areas
  gen_pcb.py finish <board.kicad_pcb> <in.ses>    takes the routed session, adds the ground pours

Run with KiCad's own Python (the one that has pcbnew).
"""
import os
import sys

import pcbnew
from pcbnew import FromMM, VECTOR2I

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

KICAD_FP = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_FP = os.path.join(os.path.dirname(HERE), "lib", "rp2040_encoder.pretty")


ROUTER_EDGE_INSET = 0.5   # mm
MIN_TRACK = 0.15          # mm
POWER_TRACK = 0.4         # mm
POWER_NETS = ("/VCC1", "/VCC1_RAW", "/?_D1")


def pt(x, y):
    return VECTOR2I(FromMM(x), FromMM(y))


def load(kind):
    lib, name = D.FP[kind]
    path = PROJECT_FP if lib == "rp2040_encoder" else os.path.join(KICAD_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit("footprint not found: %s:%s" % (lib, name))
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def poly_zone(board, points, layers):
    zone = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    zone.SetLayerSet(ls)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in points:
        outline.Append(FromMM(x), FromMM(y))
    board.Add(zone)
    return zone


def rect_zone(board, x1, y1, x2, y2, layers):
    return poly_zone(board, ((x1, y1), (x2, y1), (x2, y2), (x1, y2)), layers)


def keepout(board, x1, y1, x2, y2, layers, name):
    zone = rect_zone(board, x1, y1, x2, y2, layers)
    zone.SetIsRuleArea(True)
    zone.SetZoneName(name)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowFootprints(False)
    for setter in ("SetDoNotAllowZoneFills", "SetDoNotAllowCopperPour"):
        if hasattr(zone, setter):
            getattr(zone, setter)(True)
            break
    return zone


def text(board, s, x, y, size=1.0, layer=pcbnew.F_SilkS, rot=0.0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size)))
    t.SetTextThickness(FromMM(size * 0.15))
    if rot:
        t.SetTextAngleDegrees(rot)
    board.Add(t)
    return t


def place(board_path, dsn_path):
    parts, nets = D.build()
    board = pcbnew.BOARD()
    board.SetFileName(board_path)

    settings = board.GetDesignSettings()
    settings.m_TrackMinWidth = FromMM(0.2)
    settings.m_MinClearance = FromMM(0.2)
    settings.m_ViasMinSize = FromMM(0.6)
    settings.m_MinThroughDrill = FromMM(0.3)
    settings.m_CopperEdgeClearance = FromMM(0.4)
    netclass = settings.m_NetSettings.GetDefaultNetclass()
    netclass.SetClearance(FromMM(0.2))
    netclass.SetTrackWidth(FromMM(0.25))
    netclass.SetViaDiameter(FromMM(0.6))
    netclass.SetViaDrill(FromMM(0.3))
    # supply nets of the machine side get wider tracks (the same class is in the project file)
    power = pcbnew.NETCLASS("Power")
    power.SetClearance(FromMM(0.2))
    power.SetTrackWidth(FromMM(POWER_TRACK))
    power.SetViaDiameter(FromMM(0.6))
    power.SetViaDrill(FromMM(0.3))
    settings.m_NetSettings.SetNetclass("Power", power)
    for pattern in POWER_NETS:
        settings.m_NetSettings.SetNetclassPatternAssignment(pattern, "Power")

    # Net names as KiCad derives them from the schematic's local labels: with a leading slash.
    net_items = {}
    for name in sorted(set(nets.values())):
        item = pcbnew.NETINFO_ITEM(board, "/" + name)
        board.Add(item)
        net_items[name] = item

    for part in parts:
        fp = load(part["kind"])
        fp.SetReference(part["ref"])
        fp.SetPath(pcbnew.KIID_PATH("/" + D.uid("symbol " + part["ref"])))   # link to the schematic symbol
        fp.SetValue(part["value"])
        board.Add(fp)
        fp.SetOrientationDegrees(part["rot"])
        fp.SetPosition(pt(part["x"], part["y"]))
        if part.get("centre"):
            # place by the middle of the pads, not by pin 1
            xs = [p.GetPosition().x for p in fp.Pads()]
            ys = [p.GetPosition().y for p in fp.Pads()]
            mid = VECTOR2I((min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2)
            fp.Move(pt(part["x"], part["y"]) - mid)
        for pad in fp.Pads():
            number = pad.GetNumber()
            net = nets.get((part["ref"], number))
            if net:
                pad.SetNet(net_items[net])
            elif number and part["kind"] in ("idc", "zero"):
                # an unconnected pin gets the net KiCad gives it from the schematic
                pin_name = "Pin_" + number if part["kind"] == "idc" else D.ZERO_PINS[int(number)]
                item = pcbnew.NETINFO_ITEM(board, "unconnected-(%s-%s-Pad%s)" % (part["ref"], pin_name, number))
                board.Add(item)
                pad.SetNet(item)
        if part["kind"] in ("hole", "r", "c", "cbulk", "db9"):
            # too dense for readable designators; they are on the fabrication layer
            fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)

    # outline; the autorouter gets it drawn smaller, so that it keeps tracks off the edge
    edge = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECT)
    edge.SetStart(pt(ROUTER_EDGE_INSET, ROUTER_EDGE_INSET))
    edge.SetEnd(pt(D.BOARD_W - ROUTER_EDGE_INSET, D.BOARD_H - ROUTER_EDGE_INSET))
    edge.SetLayer(pcbnew.Edge_Cuts)
    edge.SetWidth(FromMM(0.1))
    board.Add(edge)

    # rule areas without copper: the isolation barrier, and the top layer under the RP2040-Zero
    layer_ids = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu}
    for name, x1, y1, x2, y2, layers in D.KEEPOUTS:
        keepout(board, x1, y1, x2, y2, tuple(layer_ids[c] for c in layers), name)

    # silkscreen
    for s, x, y, size, rot in D.SILK:
        text(board, s, x, y, size, rot=rot)
    s, x, y, size = D.BOTTOM_SILK
    text(board, s, x, y, size, layer=pcbnew.B_SilkS).SetMirrored(True)

    if not pcbnew.ExportSpecctraDSN(board, dsn_path):
        raise SystemExit("DSN export failed")
    edge.SetStart(pt(0, 0))
    edge.SetEnd(pt(D.BOARD_W, D.BOARD_H))
    pcbnew.SaveBoard(board_path, board)
    print("placed %d footprints, %d nets; wrote %s" % (len(parts), len(net_items), os.path.basename(dsn_path)))
    for ref in ("J1", "J2", "J7", "U3"):
        fp = board.FindFootprintByReference(ref)
        pads = {p.GetNumber(): (round(pcbnew.ToMM(p.GetPosition().x), 2), round(pcbnew.ToMM(p.GetPosition().y), 2))
                for p in fp.Pads()}
        print(" ", ref, {k: pads[k] for k in ("1", "2", "5", "9", "10", "21", "23") if k in pads})


def finish(board_path, ses_path):
    board = pcbnew.LoadBoard(board_path)
    if not pcbnew.ImportSpecctraSES(board, ses_path):
        raise SystemExit("SES import failed")
    # the router necks tracks down at fine-pitch pads; nothing thinner than the board's minimum
    for track in board.GetTracks():
        if track.GetClass() != "PCB_VIA" and track.GetWidth() < FromMM(MIN_TRACK):
            track.SetWidth(FromMM(MIN_TRACK))
    nets = board.GetNetsByName()
    for name, outline in D.ZONES:
        zone = poly_zone(board, outline, (pcbnew.F_Cu, pcbnew.B_Cu))
        zone.SetNet(nets["/" + name])
        zone.SetZoneName(name)
        zone.SetLocalClearance(FromMM(0.3))
        zone.SetMinThickness(FromMM(0.25))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        zone.SetThermalReliefGap(FromMM(0.3))
        zone.SetThermalReliefSpokeWidth(FromMM(0.4))
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(board_path, board)
    tracks = [t for t in board.GetTracks()]
    vias = sum(1 for t in tracks if t.GetClass() == "PCB_VIA")
    print("routed: %d track segments, %d vias; zones filled" % (len(tracks) - vias, vias))


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("place", "finish"):
        raise SystemExit(__doc__)
    {"place": place, "finish": finish}[sys.argv[1]](sys.argv[2], sys.argv[3])
