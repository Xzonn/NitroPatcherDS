import argparse
import struct
from PIL import Image, ImageDraw
from ndspy.nftr import NFTR


parser = argparse.ArgumentParser()
parser.add_argument("icon")
args = parser.parse_args()
icon = Image.open(args.icon).convert("RGBA").resize((32, 32), Image.Resampling.LANCZOS)
indexed = icon.convert("RGB").quantize(colors=15)
bitmap = Image.new("P", (32, 32))
bitmap.putpalette([0, 0, 0] + indexed.getpalette()[:45] + [0] * (256 * 3 - 48))
bitmap.putdata(
    [
        color + 1 if alpha >= 128 else 0
        for color, (_, _, _, alpha) in zip(indexed.get_flattened_data(), icon.get_flattened_data())
    ]
)
bitmap.save("assets/icon.bmp")
font = NFTR.fromFile("assets/fonts/song12.nftr")
characters = font.characterMap
widths = font.widths
screen = Image.new("RGB", (256, 384), "white")


def centered(text, y):
    width = sum(widths[characters[ord(character)]].charWidth for character in text)
    x = (256 - width) // 2
    for character in text:
        index = characters[ord(character)]
        glyph = font.glyphs[index].renderAsImage()
        screen.paste(
            "black",
            (x + widths[index].left, y + 10 - (font.cglpUnknown & 255)),
            glyph.point(lambda value: 255 - value),
        )
        x += widths[index].charWidth


screen.paste(icon.convert("RGB"), (112, 52), icon.getchannel("A"))
centered("NitroPatcherDS", 100)
centered("by Xzonn", 122)
centered("...", 192 + 72)
ImageDraw.Draw(screen).rectangle((32, 192 + 96, 223, 192 + 103), outline="black")
pixels = [0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) for r, g, b in screen.get_flattened_data()]
with open("assets/startup.bin", "wb") as writer:
    writer.write(struct.pack(f"<{len(pixels)}H", *pixels))
