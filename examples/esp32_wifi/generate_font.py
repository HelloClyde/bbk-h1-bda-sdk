"""Lossless H1WiFi16 (SIL OFL 1.1) table from the included Unifont subset."""
from pathlib import Path

root = Path(__file__).resolve().parent
glyphs = [(int(k, 16), v) for k, v in
          (line.split(":") for line in (root / "fonts/H1WiFi16.hex").read_text().splitlines())]
assert [cp for cp, _ in glyphs[:95]] == list(range(32, 127)), "ASCII fast path requires first 95 glyphs"
assert len({cp for cp, _ in glyphs}) == len(glyphs)
header = ["/* H1WiFi16: GNU Unifont 16.0.04 subset, SIL OFL 1.1.",
          " * Roman Czyborra, Paul Hardy and Unifont contributors; see fonts/. */",
          "#include <stdint.h>", f"#define BILI_FONT_COUNT {len(glyphs)}u",
          "static const uint16_t bili_font_cp[BILI_FONT_COUNT]={",
          ",".join(str(k) for k, _ in glyphs) + "};",
          "static const uint8_t bili_font_width[BILI_FONT_COUNT]={",
          ",".join(str(len(v)//4) for _, v in glyphs) + "};",
          "static const uint16_t bili_font_rows[BILI_FONT_COUNT][16]={"]
for _, bits in glyphs:
    step = len(bits)//16
    header.append("{" + ",".join(f"0x{int(bits[i:i+step],16) << (8 if step==2 else 0):04x}"
                               for i in range(0, len(bits), step)) + "},")
header.append("};")
(root / "font.h").write_text("\n".join(header)+"\n", encoding="utf-8")
