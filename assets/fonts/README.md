# 12px interface fonts

Four independent NFTR files share the union of the source character sets:

- `song12.nftr`: SimSun, then MS Gothic, then MingLiU, then Gulim.
- `japanese12.nftr`: the base font with MS Gothic replacing matching glyphs.
- `traditional12.nftr`: the base font with MingLiU replacing matching glyphs.
- `korean12.nftr`: the base font with Gulim replacing matching glyphs.

Pillow renders at 12 pixels using the embedded bitmaps where present.
Glyphs use 12×13 cells to preserve pixels above the 12px line, with the source
baseline, signed left bearings, bitmap widths and advance widths. Other interface languages use
`song12.nftr`.

Gulim uses the original source font from the Gulim and Dotum project:
https://github.com/googlefonts/gulim/tree/main/sources/ttf
See `Gulim-OFL.txt` and `Gulim-AUTHORS.txt`.

Generation uses Pillow, fonttools and the Xzonn ndspy NFTR writer:

```powershell
python source/build_font.py simsun.ttc assets/fonts/song12.nftr --japanese-font msgothic.ttc --traditional-chinese-font mingliu.ttc --korean-font Gulim.ttf
```

The output is a UTF-16 NFTR font with 12×13 cells, 2bpp monochrome glyphs,
and character widths stored in CWDH. The build embeds the font in the application.
