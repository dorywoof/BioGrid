import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SIZE = 128
SCALE = 4
S = SIZE * SCALE
CX = S / 2

BODY = (196, 150, 92)
BODY_DARK = (122, 82, 44)
BAND = (78, 50, 28)
EYE = (186, 28, 24)
EYE_DARK = (110, 12, 12)
LEG = (70, 48, 30)


def ellipse(draw, cx, cy, rx, ry, fill, outline=None, width=0):
    draw.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=fill, outline=outline, width=width)


def layer():
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return image, ImageDraw.Draw(image)


def legs():
    image, draw = layer()
    joints = [(178, (78, 168), (96, 122)), (214, (110, 222), (138, 246)), (248, (92, 300), (112, 372))]
    for sx in (-1, 1):
        for hip_y, (kx, ky), (fx, fy) in joints:
            hip = (CX + sx * 30, hip_y)
            knee = (CX + sx * kx, ky)
            foot = (CX + sx * fx, fy)
            draw.line([hip, knee], fill=LEG + (255,), width=9)
            draw.line([knee, foot], fill=LEG + (255,), width=6)
            ellipse(draw, *knee, 6, 6, LEG + (255,))
    return image


def wing_outline(hinge, angle, length, width, steps=72):
    hx, hy = hinge
    dx, dy = math.sin(angle), math.cos(angle)
    points = []
    for k in range(steps):
        t = 2 * math.pi * k / steps
        along = length / 2 * (1 - math.cos(t))
        across = width / 2 * math.sin(t)
        points.append((hx + dx * along - dy * across, hy + dy * along + dx * across))
    return points


def wings():
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for sx in (-1, 1):
        wing, draw = layer()
        hinge = (CX + sx * 22, 236)
        angle = sx * 0.32
        outline = wing_outline(hinge, angle, 236, 104)
        draw.polygon(outline, fill=(205, 218, 235, 110))
        draw.line(outline + outline[:1], fill=(150, 162, 180, 210), width=4)
        for spread in (-0.16, -0.05, 0.06, 0.16):
            end = (hinge[0] + math.sin(angle + spread) * 220, hinge[1] + math.cos(angle + spread) * 220)
            draw.line([hinge, end], fill=(125, 135, 155, 150), width=3)
        image.alpha_composite(wing)
    return image


def abdomen():
    image, draw = layer()
    ellipse(draw, CX, 330, 62, 98, BODY + (255,))
    for k, y in enumerate((280, 312, 344, 376, 404)):
        half = math.sqrt(max(0.0, 1 - ((y - 330) / 98) ** 2)) * 62
        draw.rectangle((CX - half, y, CX + half, y + 13 - k), fill=BAND + (255,))
    mask, mdraw = layer()
    ellipse(mdraw, CX, 330, 62, 98, (255, 255, 255, 255))
    clipped = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    clipped.paste(image, mask=mask.split()[3])
    shade, sdraw = layer()
    ellipse(sdraw, CX - 18, 300, 22, 50, (255, 240, 210, 70))
    clipped.alpha_composite(shade.filter(ImageFilter.GaussianBlur(10)))
    return clipped


def thorax():
    image, draw = layer()
    ellipse(draw, CX, 220, 64, 70, BODY_DARK + (255,))
    ellipse(draw, CX, 214, 56, 60, BODY + (255,))
    for sx in (-1, 1):
        draw.line([(CX + sx * 16, 168), (CX + sx * 20, 268)], fill=BODY_DARK + (160,), width=6)
    glow, gdraw = layer()
    ellipse(gdraw, CX - 14, 196, 20, 26, (255, 240, 210, 110))
    image.alpha_composite(glow.filter(ImageFilter.GaussianBlur(8)))
    return image


def head():
    image, draw = layer()
    ellipse(draw, CX, 134, 50, 32, BODY + (255,))
    for sx in (-1, 1):
        ellipse(draw, CX + sx * 40, 128, 26, 30, EYE_DARK + (255,))
        ellipse(draw, CX + sx * 39, 126, 22, 26, EYE + (255,))
        ellipse(draw, CX + sx * 33, 116, 7, 8, (255, 160, 150, 200))
        draw.line([(CX + sx * 10, 110), (CX + sx * 18, 84)], fill=BODY_DARK + (255,), width=6)
        draw.line([(CX + sx * 18, 84), (CX + sx * 34, 70)], fill=BODY_DARK + (220,), width=3)
    ellipse(draw, CX, 112, 14, 10, BODY_DARK + (255,))
    return image


def single_wing():
    wing, draw = layer()
    hinge = (CX, 20)
    outline = wing_outline(hinge, 0.0, 236, 104)
    draw.polygon(outline, fill=(205, 218, 235, 150))
    draw.line(outline + outline[:1], fill=(150, 162, 180, 230), width=4)
    for spread in (-0.16, -0.05, 0.06, 0.16):
        end = (hinge[0] + math.sin(spread) * 220, hinge[1] + math.cos(spread) * 220)
        draw.line([hinge, end], fill=(125, 135, 155, 170), width=3)
    box = (int(CX - 60), 0, int(CX + 60), 280)
    cropped = wing.crop(box)
    return cropped.resize((cropped.width // SCALE, cropped.height // SCALE), Image.LANCZOS)


def compose(parts, shadow=True):
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if shadow:
        blur, sdraw = layer()
        ellipse(sdraw, CX + 10, 270, 90, 170, (0, 0, 0, 90))
        canvas.alpha_composite(blur.filter(ImageFilter.GaussianBlur(24)))
    for part in parts:
        canvas.alpha_composite(part)
    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description="Draw the top-down Drosophila sprites used in the arena.")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "biogrid" / "assets")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    compose((legs(), abdomen(), wings(), thorax(), head())).save(args.out / "fly.png")
    compose((legs(), abdomen(), thorax(), head()), shadow=False).save(args.out / "fly_body.png")
    single_wing().save(args.out / "fly_wing.png")
    print(f"saved fly.png, fly_body.png, fly_wing.png to {args.out}")


if __name__ == "__main__":
    main()
