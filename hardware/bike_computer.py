"""
Ride Log bike computer: parametric 3D-printable enclosure (CadQuery).

    pip install cadquery
    python bike_computer.py           -> writes STEP + STL files into ./out/

STEP files open in SolidWorks, Fusion 360, Onshape, FreeCAD and Shapr3D.
STL files go straight into a slicer (PrusaSlicer, Bambu Studio, Cura).

Layout, front to back:
  clear lens (1 mm) | front face | Sharp memory display board | foam |
  electronics layer (XIAO ESP32-S3, GPS, barometer) | battery | back cover
The four corner pillars press the display board against the front face, and the
back cover screws into them (M2.5 heat-set inserts).

!! Measure your parts before printing. Values marked ASSUMED were not confirmed
!! from a datasheet; change them here and re-run.
"""
import os
import cadquery as cq

# ---------------------------------------------------------------- parts (mm)
DISP_PCB_W, DISP_PCB_H = 72.0, 46.0   # Adafruit 4694 breakout board: ASSUMED, measure yours
DISP_STACK = 4.0                      # board + glass panel + parts on the back: ASSUMED
ACTIVE_W, ACTIVE_H = 58.8, 35.3       # LS027B7DH01 viewable area (datasheet)
ACTIVE_OFFSET_X, ACTIVE_OFFSET_Y = 0.0, 0.0   # viewable area offset from board centre: ASSUMED centred

GPS = (25.5, 25.4, 8.2)               # Adafruit Mini GPS PA1010D (w, h, thickness)
XIAO = (21.0, 17.8, 4.5)              # Seeed XIAO ESP32-S3 (incl. USB-C)
BARO = (25.4, 17.8, 4.6)              # Adafruit BMP390 STEMMA QT: ASSUMED standard 1.0"x0.7" board
BATTERY = (50.0, 34.0, 6.0)           # 603450 LiPo, ~1200 mAh
BUTTON_HOLE = 7.0                     # 7 mm panel-mount momentary push button
LENS_T = 1.0                          # clear acrylic / polycarbonate window

# ---------------------------------------------------------------- case (mm)
WALL = 2.2
FRONT_T = 1.6          # front face thickness
CLEAR = 0.4            # gap around the display board
CORNER_R = 7.0
IN_CORNER_R = 1.0      # nearly square inside so the display board's corners fit
INNER_DEPTH = 22.5     # inside depth of the front shell, front face to back-cover seat
BACK_T = 2.4           # back cover plate
LIP_H, LIP_T, LIP_GAP = 2.0, 1.2, 0.3
PILLAR_D, INSERT_D = 6.6, 3.6   # 3.6 mm suits most M2.5 heat-set inserts
SCREW_D, SCREW_HEAD_D = 2.8, 5.2
FOAM = 1.0             # foam pad behind the display

IN_W = DISP_PCB_W + 2 * CLEAR
IN_H = DISP_PCB_H + 2 * CLEAR
OUT_W = IN_W + 2 * WALL
OUT_H = IN_H + 2 * WALL
SHELL_D = FRONT_T + INNER_DEPTH
PILLAR_START = FRONT_T + DISP_STACK + FOAM          # pillars clamp the display from here back
PX, PY = IN_W / 2 - PILLAR_D / 2 + 0.3, IN_H / 2 - PILLAR_D / 2 + 0.3
PILLARS = [(sx * PX, sy * PY) for sx in (-1, 1) for sy in (-1, 1)]

# z = 0 is the outside of the front face; +z goes towards the back of the unit


def rounded_box(w, h, d, r):
    return cq.Workplane("XY").rect(w, h).extrude(d).edges("|Z").fillet(r)


def front_shell():
    shell = rounded_box(OUT_W, OUT_H, SHELL_D, CORNER_R)
    cavity = rounded_box(IN_W, IN_H, INNER_DEPTH + 1, IN_CORNER_R).translate((0, 0, FRONT_T))
    shell = shell.cut(cavity)
    # soften the front edge
    shell = shell.faces("<Z").edges().fillet(1.2)

    # display window + recess for the clear lens
    win = (cq.Workplane("XY").rect(ACTIVE_W + 1.0, ACTIVE_H + 1.0).extrude(FRONT_T + 1)
           .edges("|Z").fillet(1.5).translate((ACTIVE_OFFSET_X, ACTIVE_OFFSET_Y, -0.5)))
    lens = (cq.Workplane("XY").rect(ACTIVE_W + 7, ACTIVE_H + 7).extrude(LENS_T)
            .edges("|Z").fillet(3).translate((ACTIVE_OFFSET_X, ACTIVE_OFFSET_Y, 0)))
    shell = shell.cut(win).cut(lens)

    # corner pillars with heat-set insert holes (start behind the display)
    for x, y in PILLARS:
        p = (cq.Workplane("XY").circle(PILLAR_D / 2).extrude(SHELL_D - PILLAR_START)
             .translate((x, y, PILLAR_START)))
        # tie each pillar into the corner walls
        tie = (cq.Workplane("XY").rect(PILLAR_D, PILLAR_D).extrude(SHELL_D - PILLAR_START)
               .translate((x + (PILLAR_D / 2 if x > 0 else -PILLAR_D / 2) * 0.6,
                           y + (PILLAR_D / 2 if y > 0 else -PILLAR_D / 2) * 0.6, PILLAR_START)))
        shell = shell.union(p.union(tie).intersect(rounded_box(IN_W + 0.01, IN_H + 0.01, SHELL_D, IN_CORNER_R)))
        hole = cq.Workplane("XY").circle(INSERT_D / 2).extrude(8).translate((x, y, SHELL_D - 8))
        shell = shell.cut(hole)

    # two buttons on the bottom edge (Lap, Start/Pause), one on the left side (Page/Power)
    btn_z = FRONT_T + DISP_STACK + FOAM + 5.0
    for x in (-22.0, 22.0):
        shell = shell.cut(cq.Workplane("XZ").center(x, btn_z).circle(BUTTON_HOLE / 2)
                          .extrude(-WALL * 3).translate((0, -OUT_H / 2 - WALL, 0)))
    shell = shell.cut(cq.Workplane("YZ").center(-10.0, btn_z).circle(BUTTON_HOLE / 2)
                      .extrude(WALL * 3).translate((-OUT_W / 2 - WALL, 0, 0)))

    # USB-C port for the XIAO, bottom edge centre
    usb_z = FRONT_T + DISP_STACK + FOAM + 2.6
    usb = (cq.Workplane("XZ").center(0, usb_z).rect(10.5, 4.6).extrude(-WALL * 3)
           .edges("|Y").fillet(1.8).translate((0, -OUT_H / 2 - WALL, 0)))
    shell = shell.cut(usb)
    return shell


def back_cover():
    plate = rounded_box(OUT_W, OUT_H, BACK_T, CORNER_R)
    # locating lip that sits inside the shell (clears the pillars)
    lip_out = rounded_box(IN_W - 2 * LIP_GAP, IN_H - 2 * LIP_GAP, LIP_H, IN_CORNER_R)
    lip_in = rounded_box(IN_W - 2 * LIP_GAP - 2 * LIP_T, IN_H - 2 * LIP_GAP - 2 * LIP_T, LIP_H, 0.5)
    lip = lip_out.cut(lip_in).translate((0, 0, -LIP_H))
    for x, y in PILLARS:
        lip = lip.cut(cq.Workplane("XY").circle(PILLAR_D / 2 + 0.5).extrude(LIP_H).translate((x, y, -LIP_H)))
    cover = plate.union(lip)

    # battery cradle (low walls) on the inside
    bw, bh, _ = BATTERY
    cradle = (rounded_box(bw + 2 * 1.2 + 0.8, bh + 2 * 1.2 + 0.8, 3.0, 2)
              .cut(rounded_box(bw + 0.8, bh + 0.8, 3.0, 1)).translate((0, 2.0, -3.0)))
    for x, y in PILLARS:
        cradle = cradle.cut(cq.Workplane("XY").circle(PILLAR_D / 2 + 0.6).extrude(3).translate((x, y, -3)))
    cover = cover.union(cradle)

    # countersunk-ish screw holes
    for x, y in PILLARS:
        cover = cover.cut(cq.Workplane("XY").circle(SCREW_D / 2).extrude(BACK_T + 1).translate((x, y, -0.5)))
        cover = cover.cut(cq.Workplane("XY").circle(SCREW_HEAD_D / 2).extrude(1.4).translate((x, y, BACK_T - 1.4)))

    # recess for a stick-on Garmin quarter-turn adapter (centre of the back)
    cover = cover.cut(rounded_box(30, 30, 0.8, 4).translate((0, 0, BACK_T - 0.8)))
    # move so z=0 of the cover meets the back of the shell
    return cover.translate((0, 0, SHELL_D))


def placeholders():
    """Simple blocks for the bought parts, for checking fit in the assembly."""
    z0 = FRONT_T
    disp = cq.Workplane("XY").rect(DISP_PCB_W, DISP_PCB_H).extrude(DISP_STACK).translate((0, 0, z0))
    lens = (cq.Workplane("XY").rect(ACTIVE_W + 6.6, ACTIVE_H + 6.6).extrude(LENS_T)
            .edges("|Z").fillet(2.8).translate((ACTIVE_OFFSET_X, ACTIVE_OFFSET_Y, 0)))
    ze = FRONT_T + DISP_STACK + FOAM
    gps = cq.Workplane("XY").rect(GPS[0], GPS[1]).extrude(GPS[2]).translate((-17.0, 9.0, ze))
    baro = cq.Workplane("XY").rect(BARO[0], BARO[1]).extrude(BARO[2]).translate((17.0, 12.0, ze))
    xiao = cq.Workplane("XY").rect(XIAO[0], XIAO[1]).extrude(XIAO[2]).translate((0, -IN_H / 2 + XIAO[1] / 2 + 0.3, ze))
    zb = SHELL_D - 3.0 - BATTERY[2]
    batt = cq.Workplane("XY").rect(BATTERY[0], BATTERY[1]).extrude(BATTERY[2]).translate((0, 2.0, zb))
    return {"display": disp, "lens": lens, "gps": gps, "barometer": baro, "xiao_esp32s3": xiao, "battery": batt}


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(out, exist_ok=True)
    fs, bc = front_shell(), back_cover()
    cq.exporters.export(fs, os.path.join(out, "front_shell.step"))
    cq.exporters.export(fs, os.path.join(out, "front_shell.stl"), tolerance=0.02, angularTolerance=0.1)
    # print the cover with its flat outside on the bed
    bc_print = bc.translate((0, 0, -SHELL_D)).rotate((0, 0, 0), (1, 0, 0), 180)
    cq.exporters.export(bc, os.path.join(out, "back_cover.step"))
    cq.exporters.export(bc_print, os.path.join(out, "back_cover.stl"), tolerance=0.02, angularTolerance=0.1)

    asm = cq.Assembly(name="ride_log_bike_computer")
    asm.add(fs, name="front_shell", color=cq.Color(0.16, 0.17, 0.19))
    asm.add(bc, name="back_cover", color=cq.Color(0.22, 0.23, 0.25))
    colors = {"display": (0.75, 0.78, 0.74), "lens": (0.7, 0.85, 1.0, 0.35), "gps": (0.1, 0.4, 0.8),
              "barometer": (0.1, 0.4, 0.8), "xiao_esp32s3": (0.1, 0.5, 0.3), "battery": (0.85, 0.6, 0.1)}
    for k, shape in placeholders().items():
        asm.add(shape, name=k, color=cq.Color(*colors[k]))
    asm.export(os.path.join(out, "assembly.step"))
    print(f"Case outside: {OUT_W:.1f} x {OUT_H:.1f} x {SHELL_D + BACK_T:.1f} mm  ->  {out}")
