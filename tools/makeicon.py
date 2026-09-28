"""Make res/icon.ico and res/icon.png from icon.png (the exe and window icon).

The white around the star becomes transparent (flood fill from the border, so
the white inside the star stays), the picture is trimmed to the star with a
small margin and saved at the sizes Windows uses.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "icon.png")
ICO = os.path.join(ROOT, "res", "icon.ico")
PNG = os.path.join(ROOT, "res", "icon.png")

im = Image.open(SRC).convert("RGBA")
w, h = im.size
# mark the background: near-white pixels connected to the border
mask = Image.new("L", (w, h), 0)
px = im.load()
seen = bytearray(w * h)
stack = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)] + [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
def whiteish(p): return p[0] > 225 and p[1] > 225 and p[2] > 225
mp = mask.load()
while stack:
    x, y = stack.pop()
    i = y * w + x
    if seen[i]: continue
    seen[i] = 1
    if not whiteish(px[x, y]): continue
    mp[x, y] = 255
    if x > 0: stack.append((x - 1, y))
    if x < w - 1: stack.append((x + 1, y))
    if y > 0: stack.append((x, y - 1))
    if y < h - 1: stack.append((x, y + 1))
out = im.copy()
op = out.load()
for y in range(h):
    for x in range(w):
        if mp[x, y]: op[x, y] = (255, 255, 255, 0)
# trim to the star, square, 4% margin
bbox = out.getbbox()
x0, y0, x1, y1 = bbox
side = max(x1 - x0, y1 - y0)
side = int(side * 1.04)
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
sq.paste(out.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side)), (0, 0))
big = sq.resize((256, 256), Image.LANCZOS)
big.save(PNG)
big.save(ICO, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("bbox", bbox, "->", PNG, ICO)
