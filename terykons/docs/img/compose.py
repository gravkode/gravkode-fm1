#!/usr/bin/env python3
"""The README's pictures from the simulator's screenshots (tests/screens.txt): the screenshots compressed, the
playing screen twice its size (pixels kept sharp), and eight pages in a grid, two across. Needs Pillow."""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SHOTS = ["screen-osc", "screen-playing", "screen-mod", "screen-env", "screen-glo", "screen-fx-hall",
         "screen-fx-tape", "screen-fx-echo", "screen-debug", "screen-seq", "screen-tracks", "screen-patterns",
         "screen-drum", "screen-shape", "screen-splash", "screen-night", "screen-day", "screen-seq-notes",
         "screen-seq-drum", "screen-arp", "screen-lock", "screen-logo"]
GAP, BACK = 8, (24, 24, 24)

for name in SHOTS:                       # the simulator writes them uncompressed
    p = HERE / f"{name}.png"
    Image.open(p).convert("RGB").save(p, optimize=True)

hero = Image.open(HERE / "screen-playing.png")
hero.resize((hero.width * 2, hero.height * 2), Image.NEAREST).save(HERE / "readme-playing.png", optimize=True)

pages = [Image.open(HERE / f"screen-{p}.png") for p in ("seq", "osc", "mod", "env", "fx-hall", "fx-echo", "glo",
                                                         "arp")]
w, h = pages[0].size
grid = Image.new("RGB", (2 * w + GAP, 4 * h + 3 * GAP), BACK)
for i, im in enumerate(pages):
    grid.paste(im, ((i % 2) * (w + GAP), (i // 2) * (h + GAP)))
grid.save(HERE / "readme-pages.png", optimize=True)
print("readme-playing.png, readme-pages.png")

icons = sorted(HERE.glob("icon-*.png"))   # tests/icon_sheet.txt's: each engine's splash, a moment after a note
if icons:
    # (0.28) a poster: each engine's icon at its biggest and its name, six across, as the engines' poster has them
    tiles = [Image.open(p).convert("RGB").crop((44, 58, 196, 150)) for p in icons]
    tw, th = tiles[0].size
    sheet = Image.new("RGB", (6 * tw + 5 * GAP, 5 * th + 4 * GAP), BACK)
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % 6) * (tw + GAP), (i // 6) * (th + GAP)))
    sheet.save(HERE / "readme-icons.png", optimize=True)
    for p in icons:
        p.unlink()
    print("readme-icons.png")
