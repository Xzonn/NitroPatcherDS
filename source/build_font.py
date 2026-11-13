import argparse
import os
from fontTools.ttLib import TTFont
from PIL import Image, ImageFont
from ndspy.nftr import NFTR, Glyph, CharacterWidth, WidthBlock, MappingBlock

parser = argparse.ArgumentParser()
parser.add_argument("font")
parser.add_argument("output")
parser.add_argument("--japanese-font", required=True)
parser.add_argument("--traditional-chinese-font", required=True)
parser.add_argument("--korean-font", required=True)
args = parser.parse_args()


def load(path):
    source = TTFont(path, fontNumber=0)
    mapping = source.getBestCmap()
    strike_index = next(
        index
        for index, strike in enumerate(source["EBLC"].strikes)
        if (strike.bitmapSizeTable.ppemX, strike.bitmapSizeTable.ppemY) == (12, 12)
    )
    strike = source["EBLC"].strikes[strike_index]
    baseline = strike.bitmapSizeTable.hori.ascender
    font = ImageFont.truetype(path, 12, index=0, layout_engine=ImageFont.Layout.BASIC)
    characters = {}
    for code, name in mapping.items():
        if code >= 65536:
            continue
        image = Image.new("1", (12, 13))
        left = glyph_width = 0
        mask, offset = font.getmask2(chr(code), mode="1", anchor="ls")
        if mask.size[0] and mask.size[1]:
            bitmap = Image.frombytes("L", mask.size, bytes(mask)).convert("1")
            bounds = bitmap.getbbox()
            if bounds:
                left = offset[0] + bounds[0]
                glyph_width = bounds[2] - bounds[0]
                image.paste(bitmap.crop(bounds), (0, baseline + 1 + offset[1] + bounds[1]))
        glyph = Glyph(12, 13, 2, bytes(39))
        glyph.setPixels([3 if pixel else 0 for pixel in image.get_flattened_data()])
        width = round(font.getlength(chr(code)))
        characters[code] = (glyph, CharacterWidth(left, glyph_width, width))
    return characters


def save(characters, path):
    result = NFTR()
    result.lineFeed = 12
    result.encoding = 1
    result.glyphWidth = 12
    result.glyphHeight = 13
    result.glyphDataLength = 39
    result.cglpUnknown = 11 | (12 << 8)
    result.bpp = 2
    result.defaultWidth = CharacterWidth(0, 12, 12)
    widths = []
    mapping = {}
    for code in sorted(characters):
        glyph, width = characters[code]
        mapping[code] = len(result.glyphs)
        result.glyphs.append(glyph)
        widths.append(width)
    result.alterCharIndex = mapping[ord("?")]
    result.widthBlocks = [WidthBlock(0, widths)]
    result.mappingBlocks = [MappingBlock(min(mapping), max(mapping), 2, mapping)]
    with open(path, "wb") as writer:
        writer.write(result.save())
    print(f"Generated {path} with {len(result.glyphs)} glyphs at 12 pixels")


song = load(args.font)
japanese = load(args.japanese_font)
traditional = load(args.traditional_chinese_font)
korean = load(args.korean_font)
base = korean | traditional | japanese | song
directory = os.path.dirname(args.output)
save(base, args.output)
save(base | japanese, f"{directory}/japanese12.nftr")
save(base | traditional, f"{directory}/traditional12.nftr")
save(base | korean, f"{directory}/korean12.nftr")
