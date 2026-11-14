#!/usr/bin/env python3

from io import BytesIO
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

UPEM = 1000
ASCENT = 800
DESCENT = -200
CELL = 600
PRINTABLE = range(0x20, 0x7F)
E_ACUTE = 0xE9
TAB = 0x09


def glyph_name(codepoint):
    return f"uni{codepoint:04X}"


def build_ttf(family, advances):
    names = [glyph_name(cp) for cp in advances]
    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder([".notdef"] + names)
    fb.setupCharacterMap({cp: glyph_name(cp) for cp in advances})
    metrics = {".notdef": (500, 0)}
    metrics.update({glyph_name(cp): (advance, 0) for cp, advance in advances.items()})
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    fb.setupNameTable({"familyName": family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT)
    fb.setupPost()
    fb.setupGlyf({name: TTGlyphPen(None).glyph() for name in [".notdef"] + names})
    buf = BytesIO()
    fb.save(buf)
    return buf.getvalue()


def build_mono_ttf():
    advances = {cp: CELL for cp in PRINTABLE}
    advances[E_ACUTE] = 900
    return build_ttf("EmUiFixedPitchTest", advances)


def build_uneven_ttf():
    advances = {cp: CELL for cp in PRINTABLE}
    advances[ord("~")] = 500
    advances[TAB] = 700
    return build_ttf("EmUiUnevenPitchTest", advances)


def c_array(name, data):
    lines = [f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02x}," for b in data[i : i + 16]))
    lines.append("};")
    lines.append(f"static const long {name}_size = {len(data)};")
    return "\n".join(lines)


def main():
    mono = build_mono_ttf()
    uneven = build_uneven_ttf()
    out = Path(__file__).resolve().parent / "fixed_pitch_test_fonts.h"
    out.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                c_array("k_fixed_pitch_test_ttf", mono),
                "",
                c_array("k_uneven_pitch_test_ttf", uneven),
                "",
            ]
        )
    )
    print(f"wrote {out} ({len(mono)} + {len(uneven)} TTF bytes)")


if __name__ == "__main__":
    main()
