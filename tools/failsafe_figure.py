"""Disegna docs/img/failsafe.jpg: i tre regimi del controllo (Normal / 2 of 3 / Fail-safe).

Disegnata via codice (non generata da AI) così numero di sonde e logica sono esatti.
Uso: python tools/failsafe_figure.py   (serve Pillow; font Segoe UI su Windows, altrimenti default)
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1680, 945
BG, PANEL = (11, 22, 40), (18, 34, 58)
TEAL, ORANGE, ICE, RED = (45, 212, 191), (251, 146, 60), (125, 211, 252), (248, 113, 113)
GREY, WHITE, MUTED = (100, 116, 139), (241, 245, 249), (148, 163, 184)
WATER, WATER_TOP, SAND = (14, 76, 120), (34, 120, 170), (120, 104, 72)


def font(size, bold=False):
    for name in (("segoeuib.ttf" if bold else "segoeui.ttf"), "arialbd.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def dashed(d, p0, p1, color, width=4, dash=14, gap=10):
    (x0, y0), (x1, y1) = p0, p1
    length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    n = int(length // (dash + gap)) + 1
    for i in range(n):
        a, b = i * (dash + gap) / length, min(1, (i * (dash + gap) + dash) / length)
        d.line([(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a), (x0 + (x1 - x0) * b, y0 + (y1 - y0) * b)],
               fill=color, width=width)


def dial(d, cx, cy, r, color):
    """Termostato interno: quadrante con lancetta."""
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=4)
    d.line([cx, cy, cx + r * 0.55, cy - r * 0.55], fill=color, width=4)
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=color)


def snowflake(d, cx, cy, r, color):
    for k in range(3):
        import math
        a = math.pi / 3 * k
        dx, dy = r * math.cos(a), r * math.sin(a)
        d.line([cx - dx, cy - dy, cx + dx, cy + dy], fill=color, width=4)


def flame(d, cx, cy, s, color):
    """Fiamma a due lingue con cuore chiaro (non una goccia)."""
    d.polygon([(cx - s * 0.1, cy - s * 1.1), (cx + s * 0.25, cy - s * 0.45), (cx + s * 0.45, cy - s * 0.85),
               (cx + s * 0.7, cy - s * 0.1), (cx + s * 0.55, cy + s * 0.55), (cx, cy + s * 0.8),
               (cx - s * 0.55, cy + s * 0.55), (cx - s * 0.7, cy - s * 0.05)], fill=color)
    d.polygon([(cx, cy - s * 0.25), (cx + s * 0.3, cy + s * 0.35), (cx, cy + s * 0.6),
               (cx - s * 0.3, cy + s * 0.35)], fill=(254, 215, 170))


def cross(d, cx, cy, r, color):
    d.line([cx - r, cy - r, cx + r, cy + r], fill=color, width=5)
    d.line([cx - r, cy + r, cx + r, cy - r], fill=color, width=5)


def panel(d, x0, title, title_color, probes, controller_ok, regulating, caption):
    """probes: lista di 3 stati 'ok' | 'out' | 'bad'."""
    pw, top = 520, 60
    d.rounded_rectangle([x0, top, x0 + pw, H - 40], 28, fill=PANEL)
    tf = font(46, True)
    tw = d.textlength(title, font=tf)
    d.text((x0 + (pw - tw) / 2, top + 24), title, font=tf, fill=title_color)

    # vasca
    tx0, tx1, ty0, ty1 = x0 + 60, x0 + 380, 470, 760
    water_y = 520
    d.rectangle([tx0, water_y, tx1, ty1], fill=WATER)
    d.rectangle([tx0, water_y, tx1, water_y + 10], fill=WATER_TOP)
    d.rectangle([tx0, ty1 - 34, tx1, ty1], fill=SAND)
    d.rectangle([tx0, ty0, tx1, ty1], outline=MUTED, width=4)

    # controller
    cx0, cy0, cx1, cy1 = x0 + 120, 180, x0 + 320, 300
    ccol = TEAL if controller_ok else RED
    d.rounded_rectangle([cx0, cy0, cx1, cy1], 16, fill=(15, 28, 48), outline=ccol, width=5)
    d.rectangle([cx0 + 30, cy0 + 22, cx1 - 30, cy0 + 72], fill=(172, 196, 150))
    if controller_ok and regulating:
        lf = font(34, True)
        d.text((cx0 + 52, cy0 + 24), "25.0°", font=lf, fill=(28, 38, 30))
    else:  # triangolo di avviso sul display
        mx = (cx0 + cx1) / 2
        d.polygon([(mx, cy0 + 26), (mx - 22, cy0 + 68), (mx + 22, cy0 + 68)], fill=(28, 38, 30))
        d.text((mx - 4, cy0 + 38), "!", font=font(26, True), fill=(172, 196, 150))

    # 3 sonde
    for i, state in enumerate(probes):
        px = cx0 + 50 + i * 50
        end = water_y + 120
        color = TEAL if state == "ok" else RED
        if state == "out":
            # sonda sollevata fuori dall'acqua, appesa al bordo
            d.line([px, cy1, px, water_y - 40], fill=color, width=5)
            d.rounded_rectangle([px - 8, water_y - 78, px + 8, water_y - 30], 6, fill=color)
            cross(d, px, water_y - 100, 11, RED)
        else:
            d.line([px, cy1, px, end - 40], fill=color, width=5)
            d.rounded_rectangle([px - 8, end - 48, px + 8, end], 6, fill=color)
            if state == "bad":
                cross(d, px, end + 22, 11, RED)

    # riscaldatore (in vasca, a sinistra) e chiller (fuori, a destra)
    hx = tx0 + 34
    d.rounded_rectangle([hx - 14, water_y + 30, hx + 14, ty1 - 50], 12, fill=ORANGE)
    flame(d, hx, water_y - 58, 26, ORANGE)
    chx0, chy0 = tx1 + 20, 560
    d.rounded_rectangle([chx0, chy0, chx0 + 100, chy0 + 140], 14, fill=(30, 58, 90), outline=ICE, width=4)
    snowflake(d, chx0 + 50, chy0 + 46, 24, ICE)
    d.line([chx0, chy0 + 110, tx1 - 4, chy0 + 110], fill=ICE, width=4)  # tubo verso la vasca

    # comando dal controller ai carichi
    left_t, right_t = (hx, water_y - 92), (chx0 + 50, chy0)
    if regulating:
        d.line([(cx0, (cy0 + cy1) / 2), (hx, (cy0 + cy1) / 2), left_t], fill=TEAL, width=5)
        d.line([(cx1, (cy0 + cy1) / 2), (chx0 + 50, (cy0 + cy1) / 2), right_t], fill=TEAL, width=5)
    else:
        dashed(d, (cx0, (cy0 + cy1) / 2), (hx, (cy0 + cy1) / 2), GREY)
        dashed(d, (hx, (cy0 + cy1) / 2), left_t, GREY)
        dashed(d, (cx1, (cy0 + cy1) / 2), (chx0 + 50, (cy0 + cy1) / 2), GREY)
        dashed(d, (chx0 + 50, (cy0 + cy1) / 2), right_t, GREY)
        # i termostati interni prendono il controllo
        dial(d, hx + 44, water_y - 60, 20, ORANGE)
        dial(d, chx0 + 50, chy0 + 100, 18, ICE)

    # pesce
    fx, fy = tx0 + 255, 690
    d.ellipse([fx - 38, fy - 18, fx + 38, fy + 18], fill=(250, 204, 21))
    d.polygon([(fx - 34, fy), (fx - 64, fy - 20), (fx - 64, fy + 20)], fill=(250, 204, 21))
    d.ellipse([fx + 18, fy - 7, fx + 26, fy + 1], fill=(15, 23, 42))

    # didascalia
    cf = font(27)
    y = 795
    for line in caption:
        lw = d.textlength(line, font=cf)
        d.text((x0 + (pw - lw) / 2, y), line, font=cf, fill=WHITE)
        y += 36


def main():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    panel(d, 30, "Normal", TEAL, ["ok", "ok", "ok"], True, True,
          ["3 probes agree:", "the controller regulates"])
    panel(d, 580, "2 of 3", ORANGE, ["ok", "ok", "out"], True, True,
          ["One probe out of the water:", "regulates on the other two + alarm"])
    panel(d, 1130, "Fail-safe", RED, ["ok", "bad", "out"], True, False,
          ["Fewer than 2 healthy probes:", "both loads ON, own thermostats"])
    out = os.path.join(ROOT, "docs", "img", "failsafe.jpg")
    img.save(out, quality=92, optimize=True)
    print(out)


if __name__ == "__main__":
    main()
