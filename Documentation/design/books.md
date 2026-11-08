# Streaming books

Long-form documents use a separate streaming layer from the fixed-slot plain
text reader.

## Source: `.fbdoc`

`.fbdoc` is UTF-8 Markdown with optional TOML front matter:

```md
+++
title = "Guide"
locale = "en"
theme = "manual"
+++

# Controls

Press **E** to interact.

- WASD - move
- LMB - use

> Hint

| Key | Action |
|-----|--------|
| E   | Use    |

Image reference: `images/controls.png`

\pagebreak
```

Supported blocks: headings, paragraphs, lists, quotes, fenced code, GFM tables,
standalone images and explicit page breaks. Inline `**bold**`, `*italic*` and
`` `code` `` become `BookSpan` records; a backslash escapes the next character.
There is no raw HTML and no runtime LaTeX.

## Cooked: `.fbybook`

`fby_bookcook --input book.fbdoc --output book.fbybook` writes a little-endian,
mmap-friendly file:

- 112-byte header with magic `FBYBOOK1`, version, table offsets and text size;
- block table, span table, style table, inline table and anchor table;
- UTF-8 text arena and metadata arena.

Offsets are relative to the file and validated by `OpenBookAsset()`. The reader
checks magic/version, table alignment and bounds, every block/span/inline range,
UTF-8 validity and block boundary alignment. Access is zero-copy: blocks and
metadata are `std::span` views into the mapped bytes.

## Runtime

`BookReader` validates and exposes blocks/anchors. `BookSession` owns the reader
and builds a fixed page index of block ranges:

- `PageCount()`, `Page()`, `SetPage()`, `NextPage()`, `PreviousPage()`,
  `GoToAnchor()`;
- pages are laid out by `TextLayout` with the caller's width, font atlas and
  font size;
- `RenderPage()` re-lays out only the line segments of that page and calls
  `BuildTextRenderItems()`.

The book's bytes are never copied into a whole-book string. Only the current
page index, block/style scratch and one `TextLayoutResult` are materialized.

## Limits

- Paragraphs split across pages at line boundaries. If a single line is taller
  than the viewport, `BookPage::Overflow` is set, page height is clamped to the
  viewport and `RenderPage()` culls quads outside it; the host can show a scroll
  hint for that page.
- Images are stored as inline asset ids; the renderer needs a sprite/atlas
  mapping for them.
- The cooker emits one anchor per heading, named by a lower-case slug of the
  heading text; `GoToAnchor()` resolves it to the page containing that block.
- Page count is materialized at open, which scans block heights but does not
  retain the text.

## Benchmarks

`fby_book_bench` cooks a generated 200-section book, then times open and 500
page renders. It is a layout/page-index benchmark, not a strict CI gate; without
a font atlas it reports zero emitted glyph items.

`fby_video_bench --file <av1.webm> --frames N` times real AV1 decode through
`VideoPlayer` and the dav1d backend. The sample size must be supplied by the
caller; the repository does not ship an AV1 fixture.
