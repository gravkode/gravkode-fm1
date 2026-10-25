#!/usr/bin/env python3
"""The README's pictures from the simulator's screenshots (tests/screens.txt): the screenshots compressed, the
playing screen twice its size (pixels kept sharp), and the four pages in a grid. Needs Pillow."""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SHOTS = ["screen-osc", "screen-playing", "screen-mod", "screen-env", "screen-glo"]
GAP, BACK = 8, (24, 24, 24)

for name in SHOTS:                       # the simulator writes them uncompressed
    p = HERE / f"{name}.png"
    Image.open(p).convert("RGB").save(p, optimize=True)

hero = Image.open(HERE / "screen-playing.png")
hero.resize((hero.width * 2, hero.height * 2), Image.NEAREST).save(HERE / "readme-playing.png", optimize=True)

pages = [Image.open(HERE / f"screen-{p}.png") for p in ("osc", "mod", "env", "glo")]
w, h = pages[0].size
grid = Image.new("RGB", (2 * w + GAP, 2 * h + GAP), BACK)
for i, im in enumerate(pages):
    grid.paste(im, ((i % 2) * (w + GAP), (i // 2) * (h + GAP)))
grid.save(HERE / "readme-pages.png", optimize=True)
print("readme-playing.png, readme-pages.png")
