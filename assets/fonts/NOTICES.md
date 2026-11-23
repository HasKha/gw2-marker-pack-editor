# Fonts

Compiled into the addon, so their licences ship with it. `tools/convert_fonts.py` subsets each to what
the addon draws, without hinting, kerning or layout tables, none of which ImGui 1.80 reads, and
compresses it into `src/viewer/ui/`. The outlines and metrics of what is kept are unchanged, and so is
the name table, which holds each font's copyright and licence.

| Files | Font | Version and source | Licence |
|---|---|---|---|
| `src/viewer/ui/font_awesome_solid.*` | Font Awesome Free Solid, © 2026 Fonticons, Inc., Reserved Font Name "Font Awesome" | `otfs/Font Awesome 7 Free-Solid-900.otf` from [FortAwesome/Font-Awesome 7.3.1](https://github.com/FortAwesome/Font-Awesome/tree/7.3.1/otfs) | SIL Open Font License 1.1, which Font Awesome applies to its font files, [licenses/Font_Awesome-LICENSE.txt](licenses/Font_Awesome-LICENSE.txt) |

The icons' names, `ICON_FA_*`, come from `IconsFontAwesome7.h` in
[juliettef/IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders), fetched by the build
and matched to this version of the font.
