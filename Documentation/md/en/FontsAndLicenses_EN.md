# Base fonts and asset licenses

**Language section:** English

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Bundled base fonts

The engine ships a small, redistributable base set. Every face is distributed
under the SIL Open Font License 1.1. The license text is included in
`Assets/Licenses` and in the build output; the OFL requires the license to travel
with the font, not a notification to the authors.

| Id | Face | Scripts / use | License |
|---|---|---|---|
| `inter.latin` | Inter Regular | Latin, Greek, Cyrillic body text | Inter-OFL |
| `inter.cyrillic` | Inter Regular | Cyrillic shaping stack | Inter-OFL |
| `noto.hebrew` | Noto Sans Hebrew | Hebrew | Noto-OFL |
| `noto.arabic` | Noto Sans Arabic | Arabic | Noto-OFL |
| `noto.devanagari` | Noto Sans Devanagari | Devanagari | Noto-OFL |
| `noto.thai` | Noto Sans Thai | Thai | Noto-OFL |
| `noto.han` | Noto Sans SC | Han, paged atlas | Noto-OFL |
| `press_start` | Press Start 2P | 8-bit display text | PressStart2P-OFL |

`Text::MountBaseFonts` mounts the catalogue into a `TextFontStack`;
`Text::BaseFontCatalog` exposes the same data to tools. The set covers Latin,
Greek, Cyrillic, Hebrew, Arabic, Devanagari, Thai and Han, but it is not a claim
of every writing system. Kana and Hangul require a project font until a matching
Noto CJK face is added.

A game can ship its own TTF/OTF or `*.family.toml` exactly as before. Custom
faces are mounted with `TextFontStack::AddFont` or `AddPagedFont`; the base paths
can be shadowed by a project's assets mount.

## 2. License registry

`Assets::LicenseRegistry` loads plain-text files from a VFS directory, by default
`Licenses`:

```text
Assets/Licenses/
  Inter-OFL.txt
  Noto-OFL.txt
  MyAsset-Author-Notice.txt
```

Accepted suffixes are `.txt`, `.md` and `.license`. The file stem is the license
id. The registry keeps the full text; the engine does not embed it into code and
does not rewrite it.

```cpp
Assets::LicenseRegistry licenses;
licenses.LoadFromVfs(vfs, "Licenses");
std::string_view text = licenses.Text("MyAsset-Author-Notice");

licenses.BindAsset("images/hero.png", "MyAsset-Author-Notice");
std::string_view assetLicense = licenses.LicenseForAsset("images/hero.png");
```

`BindBaseFontLicenses` loads the bundled licenses and binds every base font to
its license id. A developer can add an author notice with `Add(name, text)` when
the text is generated or stored elsewhere; no C++ source change is required for
ordinary files.

## 3. Build output

The build copies `Assets/Licenses` to `build/<config>/bin/Licenses`. A shipped
game can read those files through the same mounted VFS and show them in a
licenses screen or print them to a text file. The engine never requires a
particular UI: the registry is a data API.

## 4. Limits

- The catalog is a curated set, not a complete Unicode font.
- Fonts with large CJK coverage use paged atlases; the first page is resident and
  later pages load on demand.
- OFL requires the license text to be distributed with the font. It does not
  require notifying the authors.
- The license registry ships text, not a legal interpretation. A project remains
  responsible for the licenses of assets it adds.