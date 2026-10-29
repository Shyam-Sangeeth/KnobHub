"""Compares two folders of screenshots (e.g. two tours) and/or puts one folder on a contact sheet. Needs Pillow.

    python -I tools/compare.py <dir> --against <before_dir>     lists the screens that differ, and by how many pixels
    python -I tools/compare.py <dir> --sheet <out.png> [--scale 0.5]

Exits with 1 when screens differ (or one is missing), so it can gate a change that must not alter 240 px screens."""
import argparse
import os
import sys

from PIL import Image, ImageChops, ImageDraw

parser = argparse.ArgumentParser()
parser.add_argument("dir")
parser.add_argument("--against")
parser.add_argument("--sheet")
parser.add_argument("--scale", type=float, default=1)
args = parser.parse_args()
names = sorted(n for n in os.listdir(args.dir) if n.endswith(".png"))
if not names:
    sys.exit(f"no PNGs in {args.dir}")
differ = 0

if args.against:
    for n in names:
        other = os.path.join(args.against, n)
        if not os.path.exists(other):
            print(f"{n}: missing in {args.against}")
            differ += 1
            continue
        a, b = Image.open(os.path.join(args.dir, n)).convert("RGB"), Image.open(other).convert("RGB")
        if a.size != b.size:
            print(f"{n}: {a.size} vs {b.size}")
            differ += 1
            continue
        r, g, bl = ImageChops.difference(a, b).split()
        count = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(lambda v: 255 if v else 0).histogram()[255]
        if count:
            print(f"{n}: {count} pixels differ")
            differ += 1
    print("all the same" if not differ else f"{differ} of {len(names)} differ")

if args.sheet:
    imgs = [Image.open(os.path.join(args.dir, n)).convert("RGB") for n in names]
    w = int(imgs[0].width * args.scale)
    cols = 5
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 8), rows * (w + 24)), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, (n, im) in enumerate(zip(names, imgs)):
        x, y = (i % cols) * (w + 8), (i // cols) * (w + 24)
        sheet.paste(im.resize((w, w)), (x, y))
        draw.text((x + 2, y + w + 4), n[:-4], fill=(220, 220, 220))
    sheet.save(args.sheet)
    print("saved", args.sheet)

sys.exit(1 if differ else 0)
