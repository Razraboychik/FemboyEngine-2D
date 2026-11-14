# Speech languages and the 42-language catalog

The narrator speaks the **content locale**, not the operating-system locale, and
it resolves voices through one policy shared by every backend: SAPI, one-core
Windows voices, AVSpeech, speech-dispatcher and voice packs.

## Catalog

`Source/Engine/UI/SpeechLanguages.hpp` lists the languages the engine documents
and tests as first-class speech targets. The catalog is not a promise that a
voice is installed; it is the list the fallback policy and this matrix are built
from. Adding a language means adding a row to `SpeechLanguages.cpp`, a row to
this table, and a case in the language test.

| Tag | Language | Native | Script | RTL |
|---|---|---|---|---|
| `en` | English | English | Latin | no |
| `ru` | Russian | Русский | Cyrillic | no |
| `de` | German | Deutsch | Latin | no |
| `fr` | French | Français | Latin | no |
| `es` | Spanish | Español | Latin | no |
| `it` | Italian | Italiano | Latin | no |
| `pt` | Portuguese | Português | Latin | no |
| `nl` | Dutch | Nederlands | Latin | no |
| `pl` | Polish | Polski | Latin | no |
| `cs` | Czech | Čeština | Latin | no |
| `sk` | Slovak | Slovenčina | Latin | no |
| `hu` | Hungarian | Magyar | Latin | no |
| `ro` | Romanian | Română | Latin | no |
| `bg` | Bulgarian | Български | Cyrillic | no |
| `uk` | Ukrainian | Українська | Cyrillic | no |
| `sr` | Serbian | Српски | Cyrillic | no |
| `hr` | Croatian | Hrvatski | Latin | no |
| `sl` | Slovenian | Slovenščina | Latin | no |
| `lt` | Lithuanian | Lietuvių | Latin | no |
| `lv` | Latvian | Latviešu | Latin | no |
| `et` | Estonian | Eesti | Latin | no |
| `fi` | Finnish | Suomi | Latin | no |
| `sv` | Swedish | Svenska | Latin | no |
| `no` | Norwegian | Norsk | Latin | no |
| `da` | Danish | Dansk | Latin | no |
| `is` | Icelandic | Íslenska | Latin | no |
| `tr` | Turkish | Türkçe | Latin | no |
| `el` | Greek | Ελληνικά | Greek | no |
| `he` | Hebrew | עברית | Hebrew | yes |
| `ar` | Arabic | العربية | Arabic | yes |
| `fa` | Persian | فارسی | Arabic | yes |
| `ur` | Urdu | اردو | Arabic | yes |
| `hi` | Hindi | हिन्दी | Devanagari | no |
| `bn` | Bengali | বাংলা | Bengali | no |
| `th` | Thai | ไทย | Thai | no |
| `vi` | Vietnamese | Tiếng Việt | Latin | no |
| `id` | Indonesian | Bahasa Indonesia | Latin | no |
| `ms` | Malay | Bahasa Melayu | Latin | no |
| `ja` | Japanese | 日本語 | Japanese | no |
| `ko` | Korean | 한국어 | Hangul | no |
| `zh` | Chinese | 中文 | Han | no |
| `ca` | Catalan | Català | Latin | no |

## Normalization and fallback

`NormalizeSpeechLanguage()` accepts `_` or `-`, lower-cases the language,
title-cases a four-letter script and upper-cases a two-letter region
(`pt_br` becomes `pt-BR`, `zh-hant` becomes `zh-Hant`).

`FindBestSpeechVoice()` picks the installed voice by this order:

1. exact canonical tag (`ru-RU` for `ru-RU`);
2. same primary subtag (`ru` or `ru-RU` for a `ru-*` request);
3. English (`en` or an `en-*` voice) as the documented last resort;
4. the first installed voice, so the narrator never goes silent when any voice
   exists at all.

`BuildSpeechLanguageFallback()` exposes the same chain for diagnostics:
`pt-BR` gives `pt-BR`, `pt`, `en`. The narrator pins the resolved voice by id;
the backend's own language search remains the fallback when a backend cannot
enumerate voices.

## What has to be installed

| Platform | Voice source | Notes |
|---|---|---|
| Windows | SAPI desktop voices and one-core voices through WinRT | Add the language under Settings, Time and language, Language and region; install its speech pack. One-core voices appear only after the language pack, SAPI voices come from third-party engines or older packs |
| macOS | AVSpeech | System Settings, Accessibility, Spoken Content, System Voice, Manage Voices; download the language |
| Linux | speech-dispatcher with an espeak-ng or Piper module | `spd-say -L` lists languages; install `speech-dispatcher` and the module for the language |
| Any | A voice pack in `Saved/VoicePacks` | The pack declares `language`; it appears in the Voice combo as `pack:<id>` and is selected by the same fallback policy |

The engine does not download voices. A language is spoken when a voice for it is
installed; otherwise the log names the fallback that was used, and the editor's
Voice combo shows what is actually available.

## Testing a language

1. Set `narration_language` in the project accessibility profile (or the Voice
   language field in the editor) to the BCP-47 tag.
2. Open the narrator panel: the Voice combo must list a voice whose language
   matches the tag, or the fallback is expected and logged.
3. Type a line in the editor's narrator preview, or run a game scene with
   `narration_world` on.
4. `fby_speech_languages_test` checks the catalog, normalization, fallback and
   voice selection for every row without touching a real synthesizer.

## Limits

- The catalog fixes the documented and tested set; it does not prevent a game
  from requesting another tag. An unknown tag still normalizes and falls back.
- Languages with a complex script (Arabic, Hebrew, Devanagari, Bengali, Thai,
  CJK) rely on the installed voice's own text handling; the engine passes UTF-8
  and never transliterates.
- Right-to-left flags in the catalog describe the script, not the voice: the
  narrator passes plain text, so the voice must handle the direction.
- A missing voice is a configuration state, not a crash: speech stays quiet and
  the log reports the chain.
