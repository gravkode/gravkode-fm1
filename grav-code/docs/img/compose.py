#!/usr/bin/env python3
"""The README's pictures from the simulator's screenshots (tests/screens.txt): the screenshots compressed, the
playing screen twice its size (pixels kept sharp), and eight pages in a grid, two across. Needs Pillow."""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SHOTS = ["screen-osc", "screen-playing", "screen-mod", "screen-env", "screen-glo", "screen-fx-grain",
         "screen-fx-mix", "screen-debug", "screen-seq", "screen-tracks", "screen-patterns",
         "screen-drum", "screen-shape", "screen-splash"]
GAP, BACK = 8, (24, 24, 24)

for name in SHOTS:                       # the simulator writes them uncompressed
    p = HERE / f"{name}.png"
    Image.open(p).convert("RGB").save(p, optimize=True)

hero = Image.open(HERE / "screen-playing.png")
hero.resize((hero.width * 2, hero.height * 2), Image.NEAREST).save(HERE / "readme-playing.png", optimize=True)

pages = [Image.open(HERE / f"screen-{p}.png") for p in ("seq", "osc", "mod", "env", "fx-grain", "fx-mix", "glo",
                                                         "debug")]
w, h = pages[0].size
grid = Image.new("RGB", (2 * w + GAP, 4 * h + 3 * GAP), BACK)
for i, im in enumerate(pages):
    grid.paste(im, ((i % 2) * (w + GAP), (i // 2) * (h + GAP)))
grid.save(HERE / "readme-pages.png", optimize=True)
print("readme-playing.png, readme-pages.png")

icons = sorted(HERE.glob("icon-*.png"))   # tests/icon_sheet.txt's: each engine's track line, icon and line
if icons:
    # The 0.22 sound header has the compact live icon beside the page title; keep partial dials out of the atlas.
    tiles = [Image.open(p).convert("RGB").crop((0, 36, 240, 82)) for p in icons]
    tw, th = tiles[0].size
    sheet = Image.new("RGB", (3 * tw + 2 * GAP, 9 * th + 8 * GAP), BACK)
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % 3) * (tw + GAP), (i // 3) * (th + GAP)))
    sheet.save(HERE / "readme-icons.png", optimize=True)
    for p in icons:
        p.unlink()
    print("readme-icons.png")
