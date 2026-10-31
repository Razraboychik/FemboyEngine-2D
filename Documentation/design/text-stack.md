# Text stack: shaping, bidi, atlases and paging

The text stack is glyph-based end to end: UTF-8 text is split into bidi and
script runs, HarfBuzz shapes each run with the face of its script, and the layout
and render passes consume glyph ids instead of codepoints. Pink UI only borrows
this path through `TextBlock` (see [Widgets.md](Widgets.md)); it does not replace
it.

## Script atlases

`TextFontStack` owns one HarfBuzz face and one atlas per script. Inter covers
Latin, Cyrillic and Greek; Noto Sans Arabic, Devanagari, Thai and SC cover the
complex scripts. Every shaped glyph carries the font id of the face that produced
it, and the render pass resolves that id to the atlas layer, so a mixed string
uses the right atlas per run.

## Bidi

`SplitRtlRuns` returns runs in visual order with a script, a UAX 9 level and a
direction. SheenBidi supplies the levels; the fallback splitter relevels for the
paragraph direction and applies rule L2 itself, and every run is split again at
script boundaries. `TextLayoutResult` keeps those runs, so inline atoms and
hit-testing place text exactly where a renderer would.

## Vertical CJK

A vertical run asks HarfBuzz for `vert`, `vrt2` and `vkrn`, which selects the
vertical punctuation glyph and its offset. Subset atlases include the Unicode
vertical presentation forms of the punctuation they carry, a vertical column is
at least one em wide, and the column origins are the same for the render pass and
for hit-testing.

## Paging

A large CJK face is not one 8192 atlas. `FontAtlasSet` is a page table of up to
sixteen 2048 pages; a page holds the glyph ids of one slice and is built from
resident TTF bytes on demand. The page budget follows the atlas edge and the em
size, replacement is least-recently-used, and the full glyph catalogue stays
addressable through the page index.

## Coverage

Each mounted face carries a coverage mask built from its cmap. `AddScriptFamily`
mounts a script family without hand-filling the stack, `RankFaces` orders the
faces of a script for the current text, and a chain of faces provides the
in-script fallback. A missing glyph moves to the next face of the same script;
`.notdef` only appears when no face covers the codepoint.

## Hot path

A shape cache keyed by text/font/size/direction/locale stores glyph and line
pools behind a fixed-capacity table. A cache hit is an array scan: it does not
shape and does not allocate. HarfBuzz buffers, the chained shaper candidate
buffer and the cache scratch live with their owners instead of on the stack, and
the counters expose hit and miss rates.

## Family and formatting

One family carries every style: IBM Plex Sans for regular, bold, italic and bold
italic, IBM Plex Mono for the mono slot, all under SIL OFL 1.1 and one font id
(`assets/fonts/IBMPlex.family.toml`). The family manifest builds one atlas per
slot; the shaped render path resolves each glyph through the style of its span,
and underline and strikethrough become decoration quads on the white layer.
Markup flags, span colours and the ECS extraction all reach the same path, and
the editor UI uses the same family.

## Preview and ECS

The editor Text Preview panel mounts the script family, rebuilds the atlases only
when the text or the pixel size changes, warms the pages of the shaped run and
paints the shared render items, so it shows the same SDF the runtime would.
`UpdateTextShapes` has a stack overload used by the ECS pass, which shapes,
routes and warms page coverage in one place.

## Tests

`text`, `text_render`, `text_format`, `text_markup`, `text_asset_format`,
`text_asset_file`, `font_builder`, `text_shaper`, `text_shape_cache`,
`text_shape_system`, `font_paging`, `text_coverage`, `text_hot_path` and
`text_golden`. The golden test locks the shaped glyph ids and advances of Latin,
Arabic, Devanagari, Thai and CJK at 16 px.
