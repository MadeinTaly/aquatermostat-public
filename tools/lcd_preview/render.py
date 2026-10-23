"""Compila l'anteprima display (stessa drawStatusScreen del firmware + Adafruit GFX)
e produce docs/img/lcd_preview.png: tutte le scene ingrandite con look LCD.
Uso: python tools/lcd_preview/render.py   (serve gcc/g++ nel PATH e Pillow)."""
import glob
import os
import subprocess
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GFX = glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "Adafruit GFX Library"))
if not GFX:
    sys.exit("Adafruit GFX non trovata: esegui prima 'pio run' per scaricare le librerie")
GFX = GFX[0]
BUILD = os.path.join(HERE, "build")
os.makedirs(BUILD, exist_ok=True)
exe = os.path.join(BUILD, "preview.exe" if os.name == "nt" else "preview")
subprocess.check_call(["g++", "-std=c++17", "-O1", "-DARDUINO=100", "-w",
                       "-I", os.path.join(HERE, "shim"), "-I", GFX, "-I", os.path.join(ROOT, "src"),
                       os.path.join(HERE, "preview.cpp"), os.path.join(ROOT, "src", "display_layout.cpp"),
                       os.path.join(GFX, "Adafruit_GFX.cpp"), "-o", exe])
subprocess.check_call([exe], cwd=BUILD)

SCALE, PAD = 6, 14
INK, PAPER = (28, 38, 30), (172, 196, 150)
scenes = sorted(glob.glob(os.path.join(BUILD, "*.pbm")))
cols = 2
w, h = 84 * SCALE, 48 * SCALE
sheet = Image.new("RGB", (cols * (w + PAD) + PAD, ((len(scenes) + 1) // cols) * (h + PAD + 14) + PAD), (30, 34, 40))
draw = ImageDraw.Draw(sheet)
for n, path in enumerate(scenes):
    rows = open(path).read().split("\n")[2:50]
    img = Image.new("RGB", (w, h), PAPER)
    d = ImageDraw.Draw(img)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            box = [x * SCALE, y * SCALE, x * SCALE + SCALE - 1, y * SCALE + SCALE - 1]
            d.rectangle(box, fill=INK if ch == "1" else PAPER)
    cx, cy = PAD + (n % cols) * (w + PAD), PAD + (n // cols) * (h + PAD + 14)
    draw.text((cx, cy), os.path.basename(path)[:-4], fill=(220, 220, 220))
    sheet.paste(img, (cx, cy + 14))
out = os.path.join(ROOT, "docs", "img", "lcd_preview.png")
sheet.save(out, optimize=True)
print(out)
