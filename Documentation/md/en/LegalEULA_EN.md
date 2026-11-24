# Project legal agreements and first-run acceptance

**Language section:** English

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Scope

`Legal::LegalService` handles the project's own legal documents: EULA, privacy
policy, terms, age gate and any other first-run agreement. It is separate from
`Assets/Licenses`, which lists third-party assets.

The shipped `Assets/Legal` text is a template. Replace it with text reviewed for
the project, jurisdiction and store before distributing a game.

## 2. Manifest

```text
Assets/Legal/
  legal.toml
  EULA_EN.txt
  EULA_RU.txt
```

```toml
[agreement]
id = "base-game-eula"
version = "1.0.0"
updated = "2026-11-24"
required = true
decline_action = "quit"

[[document]]
language = "en"
title = "End User License Agreement"
path = "Legal/EULA_EN.txt"

[[document]]
language = "ru"
title = "Лицензионное соглашение конечного пользователя"
path = "Legal/EULA_RU.txt"
```

`decline_action` is `quit`, `continue` or `custom`. The engine does not invent a
policy; the game decides what decline means.

## 3. API

```cpp
Legal::LegalService legal;
legal.LoadFromVfs(vfs, "Legal");

Legal::LegalAcceptance accepted;
Legal::LegalService::LoadAcceptance("legal_acceptance.toml", &accepted);

if (legal.NeedsAcceptance(accepted))
{
    ShowAgreement(legal.DocumentFor(locale));
}

accepted = legal.Accept("2026-11-24T00:00:00Z", locale);
Legal::LegalService::SaveAcceptance("legal_acceptance.toml", accepted);
```

`NeedsAcceptance` compares:

- agreement id;
- version;
- content hash of every localized document.

A changed version or text forces a new prompt. The acceptance stores the
language and timestamp for support/audit; it stores no personal data.

## 4. First-run gate

The engine exposes data and state, not a forced UI. A game can use its own
screen, ImGui, or a store-provided flow:

1. load the manifest;
2. load acceptance;
3. if required and not accepted, show the localized document;
4. accept → save; decline → `OnDecline()` policy;
5. continue into language/accessibility setup.

An `--accept-eula` development flag may be provided by the app for CI; it must
not be enabled in a store build.

## 5. GOG and other stores

Stores may supply their own EULA or terms file. The app can:

- prefer a store-provided `EULA.txt` when it exists;
- keep the engine manifest as fallback;
- package `Legal/` next to `Licenses/` in the build and `.fempak`.

The build target `fby_licenses` copies both `Assets/Licenses` and `Assets/Legal`
to `build/<config>/bin/`.

## 6. Universal engine rules

- no hardcoded EULA text;
- no hardcoded language list;
- no required ImGui dependency;
- acceptance is per-install/per-user, not global;
- changing text/version re-prompts;
- missing required document is an error, not a silent skip;
- the project owns the legal wording.

## 7. Limits

This is a distribution and consent mechanism, not legal advice. A lawyer must
review the final EULA, privacy policy, age requirements, refund policy and
store-specific terms. The template is not a binding agreement.