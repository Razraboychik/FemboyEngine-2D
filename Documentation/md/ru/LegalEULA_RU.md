# Юридические соглашения проекта и принятие при первом запуске

**Language section:** Русский

**Version:** 0.6.0-alpha.1
**Status:** Alpha

## 1. Назначение

`Legal::LegalService` отвечает за собственные юридические документы проекта:
EULA, privacy policy, terms, возрастной gate и другие соглашения первого
запуска. Это отдельный слой от `Assets/Licenses`, где перечислены сторонние
ассеты.

Поставляемый текст в `Assets/Legal` — шаблон. Перед распространением игры
замените его на текст, проверенный для проекта, юрисдикции и магазина.

## 2. Манифест

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

`decline_action`: `quit`, `continue` или `custom`. Движок не навязывает
политику; игра решает, что означает отказ.

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

`NeedsAcceptance` сравнивает:

- id соглашения;
- версию;
- content hash всех локализованных документов.

Изменение версии или текста требует повторного подтверждения. Acceptance
хранит язык и timestamp для поддержки/аудита; персональные данные не хранятся.

## 4. First-run gate

Движок отдаёт данные и состояние, а не навязывает UI. Игра может использовать
свой экран, ImGui или flow магазина:

1. загрузить манифест;
2. загрузить acceptance;
3. если required и не принято — показать локализованный документ;
4. accept → сохранить; decline → политика `OnDecline()`;
5. перейти к выбору языка/accessibility.

Флаг `--accept-eula` может быть у приложения для CI; в store-сборке его быть не
должно.

## 5. GOG и другие магазины

Магазин может поставлять свой EULA/terms. Приложение может:

- предпочесть `EULA.txt` от магазина, если он есть;
- оставить манифест движка как fallback;
- упаковать `Legal/` рядом с `Licenses/` в build и `.fempak`.

Цель `fby_licenses` копирует `Assets/Licenses` и `Assets/Legal` в
`build/<config>/bin/`.

## 6. Правила универсальности

- текст EULA не захардкожен;
- список языков не захардкожен;
- нет обязательной зависимости от ImGui;
- acceptance per-install/per-user, не глобальный;
- изменение текста/версии снова показывает соглашение;
- отсутствующий обязательный документ — ошибка, не тихий пропуск;
- юридические формулировки принадлежат проекту.

## 7. Ограничения

Это механизм распространения и согласия, а не юридическая консультация. Финальный
EULA, privacy policy, возрастные требования, политика возврата и условия
магазина должны быть проверены юристом. Шаблон не является обязывающим
соглашением.