"""
Ride Log bike computer: parametric 3D-printable enclosure (CadQuery).

    pip install cadquery
    python bike_computer.py                 -> both versions
    python bike_computer.py v1_epaper       -> only the cheap AliExpress v1
    python bike_computer.py adafruit_sharp  -> only the Adafruit / Sharp version

Writes STEP files (open in SolidWorks, Fusion 360, Onshape, FreeCAD, Shapr3D) and STL files
(for the slicer) into ./out/<version>/.

Layout, front to back:
  clear lens (1 mm) | front face | display board | foam |
  electronics layer (XIAO ESP32-S3, GPS, barometer) | battery | back cover
Four corner pillars press the display against the front face; the back cover screws into
them (M2.5 heat-set inserts).

!! Measure your parts before printing. Values marked ASSUMED were not confirmed from a
!! datasheet; change them here and re-run.
"""
import os
import sys
import cadquery as cq

VARIANTS = {
    # Cheap v1: AliExpress parts
    "v1_epaper": dict(
        DISP_PCB=(89.5, 38.0),            # Waveshare 2.9" e-Paper Module (V2) board (datasheet)
        DISP_STACK=4.5,                   # board + panel, plug side excluded: ASSUMED, measure yours
        ACTIVE=(66.89, 29.05),            # viewable area (datasheet)
        ACTIVE_OFFSET=(-3.0, 0.0),        # viewable area offset from board centre: ASSUMED, measure yours
        ELEC_DEPTH=6.0,                   # tallest part is the GPS board (5.3 mm)
        PARTS={                           # name: (w, h, thickness, x, y), electronics layer
            "gps_antenna": (25.0, 25.0, 4.5, -29.5, 0.0),     # ATGM336H ceramic patch antenna (typical 25x25)
            "gps": (16.2, 13.8, 5.3, -8.5, 10.0),             # ATGM336H board (Tinytronics listing size)
            "barometer": (15.0, 11.5, 2.6, 15.0, 11.0),       # GY-BMP280 board: ASSUMED
            "xiao_esp32s3": (21.0, 17.5, 4.5, 0.0, None),     # y=None: against the bottom wall (USB-C slot)
            "power_switch": (8.6, 3.6, 3.6, 40.5, -6.0),      # SS12D00 mini slide switch, behind the right-side slot
        },
        BUTTONS=[("bottom", 14.0), ("bottom", 28.0), ("top", 32.0)],   # (edge, x or y)
        SWITCH_SLOT=("right", -6.0),
        BATTERY=(50.0, 34.0, 6.0, 0.0),   # 603450 LiPo (w, h, t, x)
    ),
    # Adafruit / Sharp memory LCD version (hardware/README.md)
    "adafruit_sharp": dict(
        DISP_PCB=(72.0, 46.0),            # Adafruit 4694 breakout: ASSUMED, measure yours
        DISP_STACK=4.0,
        ACTIVE=(58.8, 35.3),              # LS027B7DH01 viewable area (datasheet)
        ACTIVE_OFFSET=(0.0, 0.0),         # ASSUMED centred
        ELEC_DEPTH=9.0,
        PARTS={
            "gps": (25.5, 25.4, 8.2, -17.0, 9.0),             # Adafruit PA1010D
            "barometer": (25.4, 17.8, 4.6, 17.0, 12.0),       # Adafruit BMP390: ASSUMED 1.0" x 0.7"
            "xiao_esp32s3": (21.0, 17.8, 4.5, 0.0, None),
        },
        BUTTONS=[("bottom", -22.0), ("bottom", 22.0), ("left", -10.0)],
        SWITCH_SLOT=None,
        BATTERY=(50.0, 34.0, 6.0, 0.0),
    ),
}

# ---------------------------------------------------------------- shared case values (mm)
WALL = 2.2
FRONT_T = 1.6          # front face thickness
CLEAR = 0.4            # gap around the display board
CORNER_R = 7.0         # outside corners
IN_CORNER_R = 1.0      # nearly square inside so the display board's corners fit
BACK_T = 2.4           # back cover plate
LIP_H, LIP_T, LIP_GAP = 2.0, 1.2, 0.3
PILLAR_D, INSERT_D = 6.6, 3.6   # 3.6 mm suits most M2.5 heat-set inserts
SCREW_D, SCREW_HEAD_D = 2.8, 5.2
FOAM = 1.0             # foam pad behind the display
BUTTON_HOLE = 7.0      # 7 mm panel-mount push buttons
LENS_T = 1.0           # clear acrylic / polycarbonate window
CRADLE_H = 3.0         # battery cradle walls on the back cover

# z = 0 is the outside of the front face; +z goes towards the back of the unit


def rounded_box(w, h, d, r):
    return cq.Workplane("XY").rect(w, h).extrude(d).edges("|Z").fillet(r)


class Case:
    def __init__(self, v):
        self.v = v
        self.in_w = v["DISP_PCB"][0] + 2 * CLEAR
        self.in_h = v["DISP_PCB"][1] + 2 * CLEAR
        self.out_w = self.in_w + 2 * WALL
        self.out_h = self.in_h + 2 * WALL
        self.elec_z = FRONT_T + v["DISP_STACK"] + FOAM                      # front of the electronics layer
        self.inner_depth = v["DISP_STACK"] + FOAM + v["ELEC_DEPTH"] + 0.5 + v["BATTERY"][2] + CRADLE_H
        self.shell_d = FRONT_T + self.inner_depth
        self.pillar_start = self.elec_z
        px = self.in_w / 2 - PILLAR_D / 2 + 0.3
        py = self.in_h / 2 - PILLAR_D / 2 + 0.3
        self.pillars = [(sx * px, sy * py) for sx in (-1, 1) for sy in (-1, 1)]
        self.button_z = self.elec_z + 4.5
        self.usb_z = self.elec_z + 2.6

    def front_shell(self):
        v = self.v
        shell = rounded_box(self.out_w, self.out_h, self.shell_d, CORNER_R)
        cavity = rounded_box(self.in_w, self.in_h, self.inner_depth + 1, IN_CORNER_R).translate((0, 0, FRONT_T))
        shell = shell.cut(cavity).faces("<Z").edges().fillet(1.2)

        aw, ah = v["ACTIVE"]; ox, oy = v["ACTIVE_OFFSET"]
        win = (cq.Workplane("XY").rect(aw + 1.0, ah + 1.0).extrude(FRONT_T + 1)
               .edges("|Z").fillet(1.5).translate((ox, oy, -0.5)))
        lens = (cq.Workplane("XY").rect(aw + 7, ah + 7).extrude(LENS_T)
                .edges("|Z").fillet(3).translate((ox, oy, 0)))
        shell = shell.cut(win).cut(lens)

        inside = rounded_box(self.in_w + 0.01, self.in_h + 0.01, self.shell_d, IN_CORNER_R)
        for x, y in self.pillars:
            p = cq.Workplane("XY").circle(PILLAR_D / 2).extrude(self.shell_d - self.pillar_start).translate((x, y, self.pillar_start))
            tie = (cq.Workplane("XY").rect(PILLAR_D, PILLAR_D).extrude(self.shell_d - self.pillar_start)
                   .translate((x + (1 if x > 0 else -1) * PILLAR_D * 0.3, y + (1 if y > 0 else -1) * PILLAR_D * 0.3, self.pillar_start)))
            shell = shell.union(p.union(tie).intersect(inside))
            shell = shell.cut(cq.Workplane("XY").circle(INSERT_D / 2).extrude(8).translate((x, y, self.shell_d - 8)))

        for edge, pos in v["BUTTONS"]:
            shell = shell.cut(self._edge_hole(edge, pos, self.button_z, cq.Workplane("XY").circle(BUTTON_HOLE / 2)))
        # USB-C port for the XIAO, bottom edge centre
        shell = shell.cut(self._edge_hole("bottom", 0.0, self.usb_z, cq.Workplane("XY").rect(10.5, 4.6), fillet=1.8))
        if v["SWITCH_SLOT"]:
            edge, pos = v["SWITCH_SLOT"]
            shell = shell.cut(self._edge_hole(edge, pos, self.elec_z + 1.8, cq.Workplane("XY").rect(6.0, 2.6), fillet=0.8))
        return shell

    def _edge_hole(self, edge, pos, z, profile, fillet=0):
        """Cut through a side wall. profile is drawn on XY, centred at the origin."""
        depth = WALL * 3
        tool = profile.extrude(depth)
        if fillet:
            tool = tool.edges("|Z").fillet(fillet)
        tool = tool.translate((0, 0, -depth / 2))
        if edge in ("bottom", "top"):
            tool = tool.rotate((0, 0, 0), (1, 0, 0), 90)            # hole axis along Y
            y = (-1 if edge == "bottom" else 1) * (self.out_h / 2 - WALL / 2)
            return tool.translate((pos, y, z))
        tool = tool.rotate((0, 0, 0), (1, 0, 0), 90).rotate((0, 0, 0), (0, 0, 1), 90)   # axis along X
        x = (-1 if edge == "left" else 1) * (self.out_w / 2 - WALL / 2)
        return tool.translate((x, pos, z))

    def back_cover(self):
        plate = rounded_box(self.out_w, self.out_h, BACK_T, CORNER_R)
        lip_out = rounded_box(self.in_w - 2 * LIP_GAP, self.in_h - 2 * LIP_GAP, LIP_H, IN_CORNER_R)
        lip_in = rounded_box(self.in_w - 2 * LIP_GAP - 2 * LIP_T, self.in_h - 2 * LIP_GAP - 2 * LIP_T, LIP_H, 0.5)
        lip = lip_out.cut(lip_in).translate((0, 0, -LIP_H))
        for x, y in self.pillars:
            lip = lip.cut(cq.Workplane("XY").circle(PILLAR_D / 2 + 0.5).extrude(LIP_H).translate((x, y, -LIP_H)))
        cover = plate.union(lip)

        bw, bh, _, bx = self.v["BATTERY"]
        cradle = (rounded_box(bw + 2 * 1.2 + 0.8, bh + 2 * 1.2 + 0.8, CRADLE_H, 2)
                  .cut(rounded_box(bw + 0.8, bh + 0.8, CRADLE_H, 1)).translate((bx, 0, -CRADLE_H)))
        for x, y in self.pillars:
            cradle = cradle.cut(cq.Workplane("XY").circle(PILLAR_D / 2 + 0.6).extrude(CRADLE_H).translate((x, y, -CRADLE_H)))
        cover = cover.union(cradle)

        for x, y in self.pillars:
            cover = cover.cut(cq.Workplane("XY").circle(SCREW_D / 2).extrude(BACK_T + 1).translate((x, y, -0.5)))
            cover = cover.cut(cq.Workplane("XY").circle(SCREW_HEAD_D / 2).extrude(1.4).translate((x, y, BACK_T - 1.4)))
        # recess for a stick-on Garmin quarter-turn adapter
        cover = cover.cut(rounded_box(30, 30, 0.8, 4).translate((0, 0, BACK_T - 0.8)))
        return cover.translate((0, 0, self.shell_d))

    def placeholders(self):
        """Simple blocks for the bought parts, for checking fit in the assembly."""
        v = self.v; out = {}
        out["display"] = cq.Workplane("XY").rect(*v["DISP_PCB"]).extrude(v["DISP_STACK"]).translate((0, 0, FRONT_T))
        aw, ah = v["ACTIVE"]; ox, oy = v["ACTIVE_OFFSET"]
        out["lens"] = (cq.Workplane("XY").rect(aw + 6.6, ah + 6.6).extrude(LENS_T).edges("|Z").fillet(2.8).translate((ox, oy, 0)))
        for name, (w, h, t, x, y) in v["PARTS"].items():
            if y is None:
                y = -self.in_h / 2 + h / 2 + 0.3
            out[name] = cq.Workplane("XY").rect(w, h).extrude(t).translate((x, y, self.elec_z))
        bw, bh, bt, bx = v["BATTERY"]
        out["battery"] = cq.Workplane("XY").rect(bw, bh).extrude(bt).translate((bx, 0, self.shell_d - CRADLE_H - bt))
        # push-button bodies (about 10 mm behind the wall)
        for i, (edge, pos) in enumerate(v["BUTTONS"]):
            body = cq.Workplane("XY").circle(3.4).extrude(10)
            if edge in ("bottom", "top"):
                s = -1 if edge == "bottom" else 1
                body = body.rotate((0, 0, 0), (1, 0, 0), -90 * s).translate((pos, s * self.in_h / 2, self.button_z))
            else:
                s = -1 if edge == "left" else 1
                body = body.rotate((0, 0, 0), (0, 1, 0), 90 * s).translate((s * self.in_w / 2, pos, self.button_z))
            out[f"button_{i + 1}"] = body
        return out


def build(name, out_root):
    c = Case(VARIANTS[name])
    out = os.path.join(out_root, name)
    os.makedirs(out, exist_ok=True)
    fs, bc = c.front_shell(), c.back_cover()
    cq.exporters.export(fs, os.path.join(out, "front_shell.step"))
    cq.exporters.export(fs, os.path.join(out, "front_shell.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(bc, os.path.join(out, "back_cover.step"))
    bc_print = bc.translate((0, 0, -c.shell_d)).rotate((0, 0, 0), (1, 0, 0), 180)   # flat outside face on the bed
    cq.exporters.export(bc_print, os.path.join(out, "back_cover.stl"), tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly(name=f"ride_log_{name}")
    asm.add(fs, name="front_shell", color=cq.Color(0.16, 0.17, 0.19))
    asm.add(bc, name="back_cover", color=cq.Color(0.22, 0.23, 0.25))
    for k, shape in c.placeholders().items():
        asm.add(shape, name=k, color=cq.Color(0.75, 0.78, 0.74) if k == "display" else cq.Color(0.2, 0.45, 0.8))
    asm.export(os.path.join(out, "assembly.step"))
    print(f"{name}: case {c.out_w:.1f} x {c.out_h:.1f} x {c.shell_d + BACK_T:.1f} mm  ->  {out}")
    return c, fs, bc


if __name__ == "__main__":
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    for n in (sys.argv[1:] or list(VARIANTS)):
        build(n, root)
