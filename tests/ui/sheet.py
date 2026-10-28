"""Convert the PPM screenshots to PNG and build one contact sheet (needs Pillow)."""
import glob, os, sys
from PIL import Image, ImageDraw

d = sys.argv[1] if len(sys.argv) > 1 else "shots"
files = sorted(glob.glob(os.path.join(d, "*.ppm")))
imgs = []
for f in files:
    im = Image.open(f)
    png = f[:-4] + ".png"
    im.save(png)
    os.remove(f)
    imgs.append((os.path.basename(png)[:-4], im))
cols, w, h, lab = 4, 320, 480, 22
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 16) + 16, rows * (h + lab + 16) + 16), (255, 255, 255))
dr = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(imgs):
    x = 16 + (i % cols) * (w + 16)
    y = 16 + (i // cols) * (h + lab + 16)
    dr.text((x, y), name, fill=(0, 0, 0))
    sheet.paste(im, (x, y + lab))
sheet.save(os.path.join(d, "sheet.png"))
print(f"{len(imgs)} screenshots -> {d}/sheet.png")
