"""Builds Knob OS's fonts (src/core/fonts/knob_font_<size>.c): LVGL's Montserrat Medium and built-in symbols, like
LVGL's own lv_font_montserrat_*, but with the characters song titles and names use beyond ASCII (accents, dashes,
curly quotes...). Needs Node.js; run from anywhere:  python knob-os/fonts/make_fonts.py"""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "core", "fonts")
CONVERTER = ["npx", "--yes", "lv_font_conv@1.5.3"]

ASCII = "0x20-0x7E"
# Latin-1 Supplement and Latin Extended-A (western and central European letters), dashes, quotes, ellipsis, bullet,
# per mille, guillemets, euro, trade mark.
TEXT = ASCII + ",0xA0-0x17F,0x2010-0x2027,0x2030,0x2039-0x203A,0x20AC,0x2122"
# LVGL's built-in symbols (LV_SYMBOL_*), from scripts/built_in_font/built_in_font_gen.py.
SYMBOLS = ("61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,"
           "61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,"
           "61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,"
           "62019,62020,62087,62099,62212,62189,62810,63426,63650")
# The big sizes only show numbers (the clock, a dial's value) and short words.
SIZES = {8: TEXT, 12: TEXT, 14: TEXT, 16: TEXT, 20: TEXT, 24: TEXT, 28: TEXT, 48: ASCII + ",0xB0"}

os.makedirs(OUT, exist_ok=True)
for size, chars in SIZES.items():
    name = f"knob_font_{size}"
    cmd = CONVERTER + ["--no-compress", "--no-prefilter", "--bpp", "4", "--size", str(size),
                       "--font", os.path.join(HERE, "Montserrat-Medium.ttf"), "-r", chars,
                       "--font", os.path.join(HERE, "FontAwesome5-Solid+Brands+Regular.woff"), "-r", SYMBOLS,
                       "--format", "lvgl", "--lv-font-name", name, "--force-fast-kern-format",
                       "-o", os.path.join(OUT, name + ".c")]
    subprocess.run(cmd, check=True, shell=os.name == "nt")
    print("wrote", name)

# Numbers bigger than the text sizes go (e.g. a counter filling the ring): digits and a few signs only, to stay small.
DIGITS = "0x20,0x25,0x2B-0x3A"  # space % + , - . / 0-9 :
for size in (36, 60, 72, 96):
    name = f"knob_font_digits_{size}"
    cmd = CONVERTER + ["--no-compress", "--no-prefilter", "--bpp", "4", "--size", str(size),
                       "--font", os.path.join(HERE, "Montserrat-Medium.ttf"), "-r", DIGITS,
                       "--format", "lvgl", "--lv-font-name", name, "--force-fast-kern-format",
                       "-o", os.path.join(OUT, name + ".c")]
    subprocess.run(cmd, check=True, shell=os.name == "nt")
    print("wrote", name)
