// FemboyEngine2D - speech language catalog and voice matching implementation.
#include "Engine/UI/SpeechLanguages.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Fby
{
namespace
{
const SpeechLanguageInfo kSpeechLanguages[] = {
    {"en", "English", "English", "Latin", false},
    {"ru", "Russian", "\u0420\u0443\u0441\u0441\u043A\u0438\u0439", "Cyrillic", false},
    {"de", "German", "Deutsch", "Latin", false},
    {"fr", "French", "Fran\u00E7ais", "Latin", false},
    {"es", "Spanish", "Espa\u00F1ol", "Latin", false},
    {"it", "Italian", "Italiano", "Latin", false},
    {"pt", "Portuguese", "Portugu\u00EAs", "Latin", false},
    {"nl", "Dutch", "Nederlands", "Latin", false},
    {"pl", "Polish", "Polski", "Latin", false},
    {"cs", "Czech", "\u010Ce\u0161tina", "Latin", false},
    {"sk", "Slovak", "Sloven\u010Dina", "Latin", false},
    {"hu", "Hungarian", "Magyar", "Latin", false},
    {"ro", "Romanian", "Rom\u00E2n\u0103", "Latin", false},
    {"bg", "Bulgarian", "\u0411\u044A\u043B\u0433\u0430\u0440\u0441\u043A\u0438", "Cyrillic", false},
    {"uk", "Ukrainian", "\u0423\u043A\u0440\u0430\u0457\u043D\u0441\u044C\u043A\u0430", "Cyrillic", false},
    {"sr", "Serbian", "\u0421\u0440\u043F\u0441\u043A\u0438", "Cyrillic", false},
    {"hr", "Croatian", "Hrvatski", "Latin", false},
    {"sl", "Slovenian", "Sloven\u0161\u010Dina", "Latin", false},
    {"lt", "Lithuanian", "Lietuvi\u0173", "Latin", false},
    {"lv", "Latvian", "Latvie\u0161u", "Latin", false},
    {"et", "Estonian", "Eesti", "Latin", false},
    {"fi", "Finnish", "Suomi", "Latin", false},
    {"sv", "Swedish", "Svenska", "Latin", false},
    {"no", "Norwegian", "Norsk", "Latin", false},
    {"da", "Danish", "Dansk", "Latin", false},
    {"is", "Icelandic", "\u00CDslenska", "Latin", false},
    {"tr", "Turkish", "T\u00FCrk\u00E7e", "Latin", false},
    {"el", "Greek", "\u0395\u03BB\u03BB\u03B7\u03BD\u03B9\u03BA\u03AC", "Greek", false},
    {"he", "Hebrew", "\u05E2\u05D1\u05E8\u05D9\u05EA", "Hebrew", true},
    {"ar", "Arabic", "\u0627\u0644\u0639\u0631\u0628\u064A\u0629", "Arabic", true},
    {"fa", "Persian", "\u0641\u0627\u0631\u0633\u06CC", "Arabic", true},
    {"ur", "Urdu", "\u0627\u0631\u062F\u0648", "Arabic", true},
    {"hi", "Hindi", "\u0939\u093F\u0928\u094D\u0926\u0940", "Devanagari", false},
    {"bn", "Bengali", "\u09AC\u09BE\u0982\u09B2\u09BE", "Bengali", false},
    {"th", "Thai", "\u0E44\u0E17\u0E22", "Thai", false},
    {"vi", "Vietnamese", "Ti\u1EBFng Vi\u1EC7t", "Latin", false},
    {"id", "Indonesian", "Bahasa Indonesia", "Latin", false},
    {"ms", "Malay", "Bahasa Melayu", "Latin", false},
    {"ja", "Japanese", "\u65E5\u672C\u8A9E", "Japanese", false},
    {"ko", "Korean", "\uD55C\uAD6D\uC5B4", "Hangul", false},
    {"zh", "Chinese", "\u4E2D\u6587", "Han", false},
    {"ca", "Catalan", "Catal\u00E0", "Latin", false},
};

constexpr std::uint32_t kSpeechLanguageCount =
    static_cast<std::uint32_t>(sizeof(kSpeechLanguages) / sizeof(kSpeechLanguages[0]));

char LowerAscii(char InChar) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(InChar)));
}

char UpperAscii(char InChar) noexcept
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(InChar)));
}

bool IsAsciiAlpha(char InChar) noexcept
{
    const unsigned char ScopedChar = static_cast<unsigned char>(InChar);
    return (ScopedChar >= 'a' && ScopedChar <= 'z') || (ScopedChar >= 'A' && ScopedChar <= 'Z');
}

bool EqualsIgnoreCase(std::string_view InLeft, std::string_view InRight) noexcept
{
    if (InLeft.size() != InRight.size())
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < InLeft.size(); ++ScopedIndex)
    {
        if (LowerAscii(InLeft[ScopedIndex]) != LowerAscii(InRight[ScopedIndex]))
        {
            return false;
        }
    }
    return true;
}

// Writes one canonical component: language lower-case, script title-case,
// region upper-case, variant lower-case. The position decides the meaning.
std::size_t WriteComponent(std::size_t InPosition, std::string_view InComponent, char* OutText, std::size_t InCapacity,
                           std::size_t InUsed) noexcept
{
    if (InComponent.empty())
    {
        return InUsed;
    }
    const bool ScopedScript = InPosition > 0 && InComponent.size() == 4;
    const bool ScopedRegion = InPosition > 0 && InComponent.size() == 2;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InComponent.size(); ++ScopedIndex)
    {
        if (InUsed + 1 >= InCapacity)
        {
            return 0;
        }
        char ScopedChar = InComponent[ScopedIndex];
        if (InPosition == 0)
        {
            ScopedChar = LowerAscii(ScopedChar);
        }
        else if (ScopedScript)
        {
            ScopedChar = ScopedIndex == 0 ? UpperAscii(ScopedChar) : LowerAscii(ScopedChar);
        }
        else if (ScopedRegion)
        {
            ScopedChar = UpperAscii(ScopedChar);
        }
        else
        {
            ScopedChar = LowerAscii(ScopedChar);
        }
        OutText[InUsed++] = ScopedChar;
    }
    return InUsed;
}
}

std::uint32_t SpeechLanguageCount() noexcept
{
    return kSpeechLanguageCount;
}

const SpeechLanguageInfo* SpeechLanguageAt(std::uint32_t InIndex) noexcept
{
    return InIndex < kSpeechLanguageCount ? &kSpeechLanguages[InIndex] : nullptr;
}

std::string_view SpeechPrimarySubtag(std::string_view InTag) noexcept
{
    if (InTag.empty())
    {
        return {};
    }
    const std::size_t ScopedSeparator = InTag.find_first_of("-_");
    return ScopedSeparator == std::string_view::npos ? InTag : InTag.substr(0, ScopedSeparator);
}

bool NormalizeSpeechLanguage(std::string_view InTag, char* OutTag, std::size_t InCapacity) noexcept
{
    if (OutTag == nullptr || InCapacity == 0)
    {
        return false;
    }
    OutTag[0] = '\0';
    std::size_t ScopedPosition = 0;
    std::size_t ScopedUsed = 0;
    std::size_t ScopedIndex = 0;
    while (ScopedIndex <= InTag.size())
    {
        const std::size_t ScopedSeparator = InTag.find_first_of("-_", ScopedIndex);
        const std::size_t ScopedEnd = ScopedSeparator == std::string_view::npos ? InTag.size() : ScopedSeparator;
        const std::string_view ScopedComponent = InTag.substr(ScopedIndex, ScopedEnd - ScopedIndex);
        if (!ScopedComponent.empty())
        {
            if (ScopedPosition > 0)
            {
                if (ScopedUsed + 1 >= InCapacity)
                {
                    OutTag[0] = '\0';
                    return false;
                }
                OutTag[ScopedUsed++] = '-';
            }
            ScopedUsed = WriteComponent(ScopedPosition, ScopedComponent, OutTag, InCapacity, ScopedUsed);
            if (ScopedUsed == 0)
            {
                OutTag[0] = '\0';
                return false;
            }
            ++ScopedPosition;
        }
        if (ScopedSeparator == std::string_view::npos)
        {
            break;
        }
        ScopedIndex = ScopedSeparator + 1;
    }
    OutTag[ScopedUsed] = '\0';
    return ScopedUsed > 0;
}

const SpeechLanguageInfo* FindSpeechLanguage(std::string_view InTag) noexcept
{
    const std::string_view ScopedPrimary = SpeechPrimarySubtag(InTag);
    if (ScopedPrimary.empty())
    {
        return nullptr;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSpeechLanguageCount; ++ScopedIndex)
    {
        if (EqualsIgnoreCase(ScopedPrimary, kSpeechLanguages[ScopedIndex].Tag))
        {
            return &kSpeechLanguages[ScopedIndex];
        }
    }
    return nullptr;
}

std::uint8_t SpeechLanguageScore(std::string_view InRequested, std::string_view InInstalled) noexcept
{
    if (InRequested.empty() || InInstalled.empty())
    {
        return 0;
    }
    char ScopedRequested[32]{};
    char ScopedInstalled[32]{};
    const bool ScopedRequestedValid = NormalizeSpeechLanguage(InRequested, ScopedRequested, sizeof(ScopedRequested));
    const bool ScopedInstalledValid = NormalizeSpeechLanguage(InInstalled, ScopedInstalled, sizeof(ScopedInstalled));
    if (ScopedRequestedValid && ScopedInstalledValid)
    {
        if (std::strcmp(ScopedRequested, ScopedInstalled) == 0)
        {
            return 3;
        }
        if (EqualsIgnoreCase(SpeechPrimarySubtag(ScopedRequested), SpeechPrimarySubtag(ScopedInstalled)))
        {
            return 2;
        }
        return 0;
    }
    if (EqualsIgnoreCase(SpeechPrimarySubtag(InRequested), SpeechPrimarySubtag(InInstalled)))
    {
        return 2;
    }
    return 0;
}

std::uint32_t BuildSpeechLanguageFallback(std::string_view InRequested,
                                          char OutChain[kMaxSpeechFallbackTags][16]) noexcept
{
    if (OutChain == nullptr)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    char ScopedRequested[16]{};
    if (InRequested.empty() || !NormalizeSpeechLanguage(InRequested, ScopedRequested, sizeof(ScopedRequested)))
    {
        std::memcpy(OutChain[ScopedCount++], "en", 3);
        return ScopedCount;
    }
    std::size_t ScopedRequestedLength = 0;
    while (ScopedRequestedLength < sizeof(ScopedRequested) && ScopedRequested[ScopedRequestedLength] != 0)
    {
        ++ScopedRequestedLength;
    }
    std::memcpy(OutChain[ScopedCount++], ScopedRequested, ScopedRequestedLength + 1);
    const std::string_view ScopedPrimary = SpeechPrimarySubtag(ScopedRequested);
    if (!EqualsIgnoreCase(ScopedPrimary, ScopedRequested) && !EqualsIgnoreCase(ScopedPrimary, OutChain[0]))
    {
        std::memcpy(OutChain[ScopedCount], ScopedPrimary.data(), ScopedPrimary.size());
        OutChain[ScopedCount][ScopedPrimary.size()] = '\0';
        ++ScopedCount;
    }
    if (!EqualsIgnoreCase(ScopedPrimary, "en") && ScopedCount < kMaxSpeechFallbackTags)
    {
        std::memcpy(OutChain[ScopedCount++], "en", 3);
    }
    return ScopedCount;
}

std::uint32_t FindBestSpeechVoice(const TtsVoiceInfo* InVoices, std::uint32_t InCount,
                                  std::string_view InRequested) noexcept
{
    if (InVoices == nullptr || InCount == 0)
    {
        return kNoSpeechVoice;
    }
    if (InRequested.empty())
    {
        return 0;
    }
    std::uint32_t ScopedBest = kNoSpeechVoice;
    std::uint8_t ScopedBestScore = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::uint8_t ScopedScore = SpeechLanguageScore(InRequested, InVoices[ScopedIndex].Language);
        if (ScopedScore > ScopedBestScore)
        {
            ScopedBestScore = ScopedScore;
            ScopedBest = ScopedIndex;
        }
    }
    if (ScopedBest != kNoSpeechVoice)
    {
        return ScopedBest;
    }
    // No installed voice claims the language. English is the last documented
    // fallback, then the first voice keeps the narrator audible at all.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        if (SpeechLanguageScore("en", InVoices[ScopedIndex].Language) >= 2)
        {
            return ScopedIndex;
        }
    }
    return 0;
}
}
