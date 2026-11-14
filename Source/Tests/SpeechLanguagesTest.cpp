// FemboyEngine2D - speech language catalog and 42-language voice fallback tests.
#include "Engine/UI/Narrator.hpp"
#include "Engine/UI/SpeechLanguages.hpp"

#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

char GLastVoiceId[192]{};
char GLastLanguage[16]{};

bool TestSetVoiceById(void*, const char* InId) noexcept
{
    std::snprintf(GLastVoiceId, sizeof(GLastVoiceId), "%s", InId != nullptr ? InId : "");
    return true;
}

void TestSetVoice(void*, const char* InLanguage) noexcept
{
    std::snprintf(GLastLanguage, sizeof(GLastLanguage), "%s", InLanguage != nullptr ? InLanguage : "");
}

std::uint32_t TestEnumerate(void*, Fby::TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    if (OutVoices == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::snprintf(OutVoices[0].Id, sizeof(OutVoices[0].Id), "voice-en");
    std::snprintf(OutVoices[0].Name, sizeof(OutVoices[0].Name), "English");
    std::snprintf(OutVoices[0].Language, sizeof(OutVoices[0].Language), "en-US");
    if (InCapacity < 2)
    {
        return 1;
    }
    std::snprintf(OutVoices[1].Id, sizeof(OutVoices[1].Id), "voice-ru");
    std::snprintf(OutVoices[1].Name, sizeof(OutVoices[1].Name), "Russian");
    std::snprintf(OutVoices[1].Language, sizeof(OutVoices[1].Language), "ru-RU");
    return 2;
}

void TestNoop(void*) noexcept
{
}

void TestSetRate(void*, float) noexcept
{
}

bool TestSpeak(void*, const char*, std::size_t, Fby::TtsPriority) noexcept
{
    return true;
}

Fby::TtsBackend MakeBackend()
{
    Fby::TtsBackend ScopedBackend{};
    ScopedBackend.speak = &TestSpeak;
    ScopedBackend.stop = &TestNoop;
    ScopedBackend.setRate = &TestSetRate;
    ScopedBackend.setVoice = &TestSetVoice;
    ScopedBackend.setVoiceById = &TestSetVoiceById;
    ScopedBackend.enumerateVoices = &TestEnumerate;
    return ScopedBackend;
}

Fby::TtsVoiceInfo MakeVoice(const char* InId, const char* InLanguage)
{
    Fby::TtsVoiceInfo ScopedVoice{};
    std::snprintf(ScopedVoice.Id, sizeof(ScopedVoice.Id), "%s", InId);
    std::snprintf(ScopedVoice.Language, sizeof(ScopedVoice.Language), "%s", InLanguage);
    return ScopedVoice;
}
}

int main()
{
    using namespace Fby;

    // Catalog: at least 42 languages, unique primary tags, complete metadata.
    FBY_CHECK(SpeechLanguageCount() >= 42);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < SpeechLanguageCount(); ++ScopedIndex)
    {
        const SpeechLanguageInfo* ScopedLanguage = SpeechLanguageAt(ScopedIndex);
        FBY_CHECK(ScopedLanguage != nullptr);
        if (ScopedLanguage == nullptr)
        {
            continue;
        }
        FBY_CHECK(ScopedLanguage->Tag != nullptr && ScopedLanguage->Tag[0] != '\0');
        FBY_CHECK(ScopedLanguage->EnglishName != nullptr && ScopedLanguage->EnglishName[0] != '\0');
        FBY_CHECK(ScopedLanguage->NativeName != nullptr && ScopedLanguage->NativeName[0] != '\0');
        FBY_CHECK(ScopedLanguage->Script != nullptr && ScopedLanguage->Script[0] != '\0');
        FBY_CHECK(FindSpeechLanguage(ScopedLanguage->Tag) == ScopedLanguage);
        char ScopedNormalized[16]{};
        FBY_CHECK(NormalizeSpeechLanguage(ScopedLanguage->Tag, ScopedNormalized, sizeof(ScopedNormalized)));
        FBY_CHECK(std::strcmp(ScopedNormalized, ScopedLanguage->Tag) == 0);
        for (std::uint32_t ScopedOther = ScopedIndex + 1; ScopedOther < SpeechLanguageCount(); ++ScopedOther)
        {
            FBY_CHECK(std::strcmp(ScopedLanguage->Tag, SpeechLanguageAt(ScopedOther)->Tag) != 0);
        }
    }

    // Right-to-left languages are flagged; the others are not.
    const SpeechLanguageInfo* ScopedArabic = FindSpeechLanguage("ar-EG");
    FBY_CHECK(ScopedArabic != nullptr && ScopedArabic->RightToLeft);
    const SpeechLanguageInfo* ScopedHebrew = FindSpeechLanguage("he");
    FBY_CHECK(ScopedHebrew != nullptr && ScopedHebrew->RightToLeft);
    const SpeechLanguageInfo* ScopedEnglish = FindSpeechLanguage("en");
    FBY_CHECK(ScopedEnglish != nullptr && !ScopedEnglish->RightToLeft);
    FBY_CHECK(FindSpeechLanguage("xx") == nullptr);
    FBY_CHECK(FindSpeechLanguage("") == nullptr);

    // BCP-47 normalization.
    char ScopedTag[32]{};
    FBY_CHECK(NormalizeSpeechLanguage("pt_br", ScopedTag, sizeof(ScopedTag)));
    FBY_CHECK(std::strcmp(ScopedTag, "pt-BR") == 0);
    FBY_CHECK(NormalizeSpeechLanguage("EN-us", ScopedTag, sizeof(ScopedTag)));
    FBY_CHECK(std::strcmp(ScopedTag, "en-US") == 0);
    FBY_CHECK(NormalizeSpeechLanguage("zh-hant", ScopedTag, sizeof(ScopedTag)));
    FBY_CHECK(std::strcmp(ScopedTag, "zh-Hant") == 0);
    FBY_CHECK(NormalizeSpeechLanguage("sr-latn-rs", ScopedTag, sizeof(ScopedTag)));
    FBY_CHECK(std::strcmp(ScopedTag, "sr-Latn-RS") == 0);
    FBY_CHECK(!NormalizeSpeechLanguage("", ScopedTag, sizeof(ScopedTag)));
    FBY_CHECK(!NormalizeSpeechLanguage("a-b-c-d-e-f-g-h-i-j-k-l-m-n", ScopedTag, 4));
    FBY_CHECK(SpeechPrimarySubtag("pt-BR") == std::string_view("pt"));
    FBY_CHECK(SpeechPrimarySubtag("ru") == std::string_view("ru"));

    // Scoring: exact, same language, unrelated.
    FBY_CHECK(SpeechLanguageScore("ru-RU", "ru-RU") == 3);
    FBY_CHECK(SpeechLanguageScore("EN-us", "en-US") == 3);
    FBY_CHECK(SpeechLanguageScore("ru-RU", "ru") == 2);
    FBY_CHECK(SpeechLanguageScore("ru", "ru-RU") == 2);
    FBY_CHECK(SpeechLanguageScore("ru", "en") == 0);

    // Fallback chain: exact, primary, English.
    char ScopedChain[kMaxSpeechFallbackTags][16]{};
    FBY_CHECK(BuildSpeechLanguageFallback("pt-BR", ScopedChain) == 3);
    FBY_CHECK(std::strcmp(ScopedChain[0], "pt-BR") == 0);
    FBY_CHECK(std::strcmp(ScopedChain[1], "pt") == 0);
    FBY_CHECK(std::strcmp(ScopedChain[2], "en") == 0);
    FBY_CHECK(BuildSpeechLanguageFallback("en-US", ScopedChain) == 2);
    FBY_CHECK(std::strcmp(ScopedChain[0], "en-US") == 0);
    FBY_CHECK(std::strcmp(ScopedChain[1], "en") == 0);
    FBY_CHECK(BuildSpeechLanguageFallback("en", ScopedChain) == 1);

    // Voice selection: exact region, same language, English fallback, first.
    TtsVoiceInfo ScopedVoices[4]{};
    ScopedVoices[0] = MakeVoice("voice-en", "en-US");
    ScopedVoices[1] = MakeVoice("voice-ru", "ru-RU");
    ScopedVoices[2] = MakeVoice("voice-de", "de-DE");
    FBY_CHECK(FindBestSpeechVoice(ScopedVoices, 3, "ru-RU") == 1);
    FBY_CHECK(FindBestSpeechVoice(ScopedVoices, 3, "ru") == 1);
    FBY_CHECK(FindBestSpeechVoice(ScopedVoices, 3, "de-AT") == 2);
    FBY_CHECK(FindBestSpeechVoice(ScopedVoices, 3, "ja-JP") == 0);
    FBY_CHECK(FindBestSpeechVoice(ScopedVoices, 3, "") == 0);
    FBY_CHECK(FindBestSpeechVoice(nullptr, 0, "en") == kNoSpeechVoice);
    // A bare language voice loses to an exact region voice only when the region
    // matches; a same-language voice still beats English.
    TtsVoiceInfo ScopedSameLanguage[2]{};
    ScopedSameLanguage[0] = MakeVoice("voice-en", "en-US");
    ScopedSameLanguage[1] = MakeVoice("voice-ru", "ru");
    FBY_CHECK(FindBestSpeechVoice(ScopedSameLanguage, 2, "ru-RU") == 1);

    // Every catalog language finds its own installed voice in a synthetic set.
    TtsVoiceInfo ScopedCatalog[64]{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < SpeechLanguageCount() && ScopedIndex < 64; ++ScopedIndex)
    {
        ScopedCatalog[ScopedIndex] = MakeVoice("voice", SpeechLanguageAt(ScopedIndex)->Tag);
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < SpeechLanguageCount() && ScopedIndex < 64; ++ScopedIndex)
    {
        FBY_CHECK(FindBestSpeechVoice(ScopedCatalog, SpeechLanguageCount(), SpeechLanguageAt(ScopedIndex)->Tag) ==
                  ScopedIndex);
    }

    // Narrator: a language change resolves to the best installed voice id, not
    // to a backend-specific language search.
    Narrator ScopedNarrator;
    const TtsBackend ScopedBackend = MakeBackend();
    ScopedNarrator.SetBackend(&ScopedBackend);
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice-en") == 0);
    NarratorSettings ScopedSettings;
    std::snprintf(ScopedSettings.Language, sizeof(ScopedSettings.Language), "ru-RU");
    ScopedNarrator.SetSettings(ScopedSettings);
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice-ru") == 0);

    if (GFailures == 0)
    {
        std::printf("speech_languages_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "speech_languages_test: %u failure(s)\n", GFailures);
    return 1;
}
