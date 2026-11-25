"""Builds index.html (the simulator) from sim_src.html + ride_logic.js + gfx_fonts.json + gfx_classic.json.

gfx_fonts.json holds the FreeSans bitmap fonts from the Adafruit GFX library (BSD licence;
glyphs from GNU FreeFont), converted so the simulator draws exactly the firmware's pixels.
"""
import os
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "sim_src.html"), encoding="utf-8").read()
fonts = open(os.path.join(here, "gfx_fonts.json"), encoding="utf-8").read()
classic = open(os.path.join(here, "gfx_classic.json"), encoding="utf-8").read()
logic = open(os.path.join(here, "ride_logic.js"), encoding="utf-8").read()
out = (src.replace("@@FONTS@@", "const GFX_FONTS = " + fonts + ";")
          .replace("@@CLASSIC@@", "const GFX_CLASSIC = " + classic + ";")
          .replace("@@RIDELOGIC@@", logic))
open(os.path.join(here, "index.html"), "w", encoding="utf-8").write(out)
print("wrote index.html", len(out) // 1024, "KB")
