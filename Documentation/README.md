# Documentation

This directory holds the FemboyEngine2D documentation set. The current public
line is **0.1.0-alpha.1**. Generated editions live below `md/`, `docx/`, `pdf/`
and `djvu/`; hand-written source documents live in this directory, `design/`,
`style/`, `release/` and `adr/`.

## Start here

| If you want to... | Read |
|---|---|
| Build the engine and open the editor | [Getting Started](md/en/FemboyEngineGettingStarted_EN.md) |
| Learn the engine by building a game | [Application Guide](md/en/FemboyEngineApplicationGuide_EN.md) |
| Look up a type, a function or a payload field | [API Reference](md/en/FemboyEngineAPIReference_EN.md) |
| Understand the architecture | [Architecture Specification](md/en/FemboyEngineArchitecture_EN.md) |
| Read the complete manual | [Complete Reference](md/en/FemboyEngineCompleteReference_EN.md) |
| Follow the C++ style contract | [Code Style](style/CodeStyle.md) |
| Plan future work | [Roadmap](ROADMAP.md) |
| Review transport security | [Security](design/security.md) |
| Work on voice, lighting, text, PIE or networking | [Design documents](design) |

Russian editions mirror the same names under `md/ru/`, `docx/ru/`, `pdf/ru/` and
`djvu/ru/`.

## Languages and formats

Every generated document is produced as two independent editions:

- English: `<Name>_EN.md`, `<Name>_EN.docx`, `<Name>_EN.pdf`, `<Name>_EN.djvu`
- Russian: `<Name>_RU.md`, `<Name>_RU.docx`, `<Name>_RU.pdf`, `<Name>_RU.djvu`

There is no combined bilingual output. The generators write only to `en/` and
`ru/` below each format directory. `CheckDocs.py` rejects a stale `bilingual/`
directory and any file outside this layout.

Four formats are produced from one source: Markdown, Word, PDF and DjVu. DjVu is
converted from the matching PDF. The preferred converter is `pdf2djvu`; when it
is unavailable, the tooling rasterises the pages with `pdftoppm` and packages
them with DjVuLibre's `c44` and `djvm`. Without either path the run skips only
DjVu and leaves the other three formats intact.

The DOCX editions use named, language-neutral styles (`FBY Title`, `FBY Body`,
`FBY Code`, and so on), and every run carries an explicit `w:lang` tag. The PDF is
exported from that language's DOCX through LibreOffice Kit, so Word and PDF never
diverge.

## Source documents

| Directory | Contents |
|---|---|
| `design/` | Security, protocol v2, voice, networking, lighting, text, PIE, accessibility, time, editor workflows, world systems and the widget catalogue |
| `style/` | `CodeStyle.md` and the CMake/C++ naming contract in `NAMING.md` |
| `adr/` | Architecture decision records `NNNN-<slug>.md` |
| `Tools/` | Markdown, DOCX, PDF and DjVu generators, link checks and reference checks |

The generated C++ manuals keep the historical `FemboyEngine*` file names so
existing links remain valid. The product name inside the documents is
FemboyEngine2D.

## Deliverables

| Document | Markdown (EN / RU) | Word (EN / RU) | PDF (EN / RU) | DjVu (EN / RU) |
|---|---|---|---|---|
| Architecture | [EN](md/en/FemboyEngineArchitecture_EN.md) / [RU](md/ru/FemboyEngineArchitecture_RU.md) | [EN](docx/en/FemboyEngineArchitecture_EN.docx) / [RU](docx/ru/FemboyEngineArchitecture_RU.docx) | [EN](pdf/en/FemboyEngineArchitecture_EN.pdf) / [RU](pdf/ru/FemboyEngineArchitecture_RU.pdf) | [EN](djvu/en/FemboyEngineArchitecture_EN.djvu) / [RU](djvu/ru/FemboyEngineArchitecture_RU.djvu) |
| Application Guide | [EN](md/en/FemboyEngineApplicationGuide_EN.md) / [RU](md/ru/FemboyEngineApplicationGuide_RU.md) | [EN](docx/en/FemboyEngineApplicationGuide_EN.docx) / [RU](docx/ru/FemboyEngineApplicationGuide_RU.docx) | [EN](pdf/en/FemboyEngineApplicationGuide_EN.pdf) / [RU](pdf/ru/FemboyEngineApplicationGuide_RU.pdf) | [EN](djvu/en/FemboyEngineApplicationGuide_EN.djvu) / [RU](djvu/ru/FemboyEngineApplicationGuide_RU.djvu) |
| API Reference | [EN](md/en/FemboyEngineAPIReference_EN.md) / [RU](md/ru/FemboyEngineAPIReference_RU.md) | [EN](docx/en/FemboyEngineAPIReference_EN.docx) / [RU](docx/ru/FemboyEngineAPIReference_RU.docx) | [EN](pdf/en/FemboyEngineAPIReference_EN.pdf) / [RU](pdf/ru/FemboyEngineAPIReference_RU.pdf) | [EN](djvu/en/FemboyEngineAPIReference_EN.djvu) / [RU](djvu/ru/FemboyEngineAPIReference_RU.djvu) |
| Complete Reference | [EN](md/en/FemboyEngineCompleteReference_EN.md) / [RU](md/ru/FemboyEngineCompleteReference_RU.md) | [EN](docx/en/FemboyEngineCompleteReference_EN.docx) / [RU](docx/ru/FemboyEngineCompleteReference_RU.docx) | [EN](pdf/en/FemboyEngineCompleteReference_EN.pdf) / [RU](pdf/ru/FemboyEngineCompleteReference_RU.pdf) | [EN](djvu/en/FemboyEngineCompleteReference_EN.djvu) / [RU](djvu/ru/FemboyEngineCompleteReference_RU.djvu) |
| CMake Project Management | [EN](md/en/CMakeProjectManagement_EN.md) / [RU](md/ru/CMakeProjectManagement_RU.md) | [EN](docx/en/CMakeProjectManagement_EN.docx) / [RU](docx/ru/CMakeProjectManagement_RU.docx) | [EN](pdf/en/CMakeProjectManagement_EN.pdf) / [RU](pdf/ru/CMakeProjectManagement_RU.pdf) | [EN](djvu/en/CMakeProjectManagement_EN.djvu) / [RU](djvu/ru/CMakeProjectManagement_RU.djvu) |
| Getting Started | [EN](md/en/FemboyEngineGettingStarted_EN.md) / [RU](md/ru/FemboyEngineGettingStarted_RU.md) | [EN](docx/en/FemboyEngineGettingStarted_EN.docx) / [RU](docx/ru/FemboyEngineGettingStarted_RU.docx) | [EN](pdf/en/FemboyEngineGettingStarted_EN.pdf) / [RU](pdf/ru/FemboyEngineGettingStarted_RU.pdf) | [EN](djvu/en/FemboyEngineGettingStarted_EN.djvu) / [RU](djvu/ru/FemboyEngineGettingStarted_RU.djvu) |
| FemboyScript | [EN](md/en/FemboyScript_EN.md) / [RU](md/ru/FemboyScript_RU.md) | [EN](docx/en/FemboyScript_EN.docx) / [RU](docx/ru/FemboyScript_RU.docx) | [EN](pdf/en/FemboyScript_EN.pdf) / [RU](pdf/ru/FemboyScript_RU.pdf) | [EN](djvu/en/FemboyScript_EN.djvu) / [RU](djvu/ru/FemboyScript_RU.djvu) |
| Project Settings | [EN](md/en/ProjectSettingsRecommendations_EN.md) / [RU](md/ru/ProjectSettingsRecommendations_RU.md) | [EN](docx/en/ProjectSettingsRecommendations_EN.docx) / [RU](docx/ru/ProjectSettingsRecommendations_RU.docx) | [EN](pdf/en/ProjectSettingsRecommendations_EN.pdf) / [RU](pdf/ru/ProjectSettingsRecommendations_RU.pdf) | [EN](djvu/en/ProjectSettingsRecommendations_EN.djvu) / [RU](djvu/ru/ProjectSettingsRecommendations_RU.djvu) |
| Viewport Modes | [EN](md/en/ViewportModes_EN.md) / [RU](md/ru/ViewportModes_RU.md) | [EN](docx/en/ViewportModes_EN.docx) / [RU](docx/ru/ViewportModes_RU.docx) | [EN](pdf/en/ViewportModes_EN.pdf) / [RU](pdf/ru/ViewportModes_RU.pdf) | [EN](djvu/en/ViewportModes_EN.djvu) / [RU](djvu/ru/ViewportModes_RU.djvu) |
| Editor Settings | [EN](md/en/EditorSettingsReference_EN.md) / [RU](md/ru/EditorSettingsReference_RU.md) | [EN](docx/en/EditorSettingsReference_EN.docx) / [RU](docx/ru/EditorSettingsReference_RU.docx) | [EN](pdf/en/EditorSettingsReference_EN.pdf) / [RU](pdf/ru/EditorSettingsReference_RU.pdf) | [EN](djvu/en/EditorSettingsReference_EN.djvu) / [RU](djvu/ru/EditorSettingsReference_RU.djvu) |

## Regenerate

Run the generators from the repository root, in this order:

```bash
python Documentation/Tools/GenerateDocumentation.py --skip-pdf
python Documentation/Tools/GenerateApplicationGuide.py --skip-pdf
python Documentation/Tools/GenerateAPIReference.py --skip-pdf
python Documentation/Tools/GenerateCompleteReference.py --skip-pdf
python Documentation/Tools/GenerateSupplementaryDocs.py
python Documentation/Tools/GenerateProjectSettingsRecommendations.py
python Documentation/Tools/MarkdownDocs.py --all
python Documentation/Tools/GenerateReferenceDocs.py
```

`BuildDocs.py` runs the full sequence and then `CheckDocs.py`:

```bash
python Documentation/Tools/BuildDocs.py --skip-pdf
```

Drop `--skip-pdf` to export DOCX, PDF and DjVu. PDF export needs `python-docx`
and a LibreOffice Kit installation. DjVu needs either `pdf2djvu`, or the
`pdftoppm`, `c44` and `djvm` tools for the raster fallback. The scripts skip only
the DjVu step when neither path is available.

## Validate

```bash
python Documentation/Tools/CheckDocs.py --strict
python Documentation/Tools/GenerateReferenceDocs.py --check
```

`CheckDocs.py` verifies links, English/Russian pairs, the four format editions,
ADR names and the absence of a bilingual tree. `GenerateReferenceDocs.py
--check` compares the editor settings and viewport manuals with their C++
sources.

## Adding a chapter

For a generated document, add one entry to `SECTIONS` in the generator, with the
English title and its Russian translation. Keep chapter numbering contiguous and
update cross-references in the same edit. Regenerate Markdown first, then DOCX and
PDF, and run `CheckDocs.py` before committing.

For an authored Markdown document, edit the `_EN.md` and `_RU.md` pair, then run
the matching generator. Do not edit files under `md/`, `docx/`, `pdf/` or `djvu/`
by hand; the next run overwrites them.
