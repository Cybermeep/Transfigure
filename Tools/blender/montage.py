# Tools/blender/montage.py - side-by-side contact sheet of preview renders (system Python + Pillow).
#   python montage.py <out.png> <view> <name1> <name2> ...
import sys, os
from PIL import Image, ImageDraw

out, view, names = sys.argv[1], sys.argv[2], sys.argv[3:]
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output", "previews")
imgs = [Image.open(os.path.join(here, "%s_%s.png" % (n, view))).convert("RGB") for n in names]
w, h = imgs[0].size
sheet = Image.new("RGB", (w * len(imgs), h + 36), (20, 22, 30))
draw = ImageDraw.Draw(sheet)
for i, (n, im) in enumerate(zip(names, imgs)):
    sheet.paste(im, (i * w, 36))
    draw.text((i * w + 12, 10), n, fill=(230, 230, 240))
sheet = sheet.resize((sheet.width // 2, sheet.height // 2))
sheet.save(out)
print("montage", out, sheet.size)
