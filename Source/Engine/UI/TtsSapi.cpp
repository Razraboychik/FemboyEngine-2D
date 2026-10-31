// FemboyEngine2D - Windows SAPI 5 text-to-speech adapter.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>

namespace Fby
{

#if defined(_WIN32)
namespace
{
[[nodiscard]] ISpVoice* Voice(void* InRaw) noexcept
{
    return static_cast<ISpVoice*>(InRaw);
}

// Bounded enumeration: a Windows installation ships a handful of voices, and a
constexpr std::uint32_t kMaxVoiceTokens = 64;
constexpr std::size_t kMaxLanguageTagBytes = 64;

// BCP-47 primary-language -> Win32 primary language id. LocaleNameToLCID()
struct LanguageCode
{
    const char* code;
    WORD primary;
};

constexpr LanguageCode kLanguageCodes[] = {
    {"af", LANG_AFRIKAANS},  {"ar", LANG_ARABIC},     {"az", LANG_AZERI},      {"be", LANG_BELARUSIAN},
    {"bg", LANG_BULGARIAN},  {"bn", LANG_BENGALI},    {"ca", LANG_CATALAN},    {"cs", LANG_CZECH},
    {"cy", LANG_WELSH},      {"da", LANG_DANISH},     {"de", LANG_GERMAN},     {"el", LANG_GREEK},
    {"en", LANG_ENGLISH},    {"es", LANG_SPANISH},    {"et", LANG_ESTONIAN},   {"eu", LANG_BASQUE},
    {"fa", LANG_PERSIAN},    {"fi", LANG_FINNISH},    {"fil", LANG_FILIPINO},  {"fr", LANG_FRENCH},
    {"ga", LANG_IRISH},      {"gl", LANG_GALICIAN},   {"gu", LANG_GUJARATI},   {"he", LANG_HEBREW},
    {"hi", LANG_HINDI},      {"hr", LANG_CROATIAN},   {"hu", LANG_HUNGARIAN},  {"hy", LANG_ARMENIAN},
    {"id", LANG_INDONESIAN}, {"is", LANG_ICELANDIC},  {"it", LANG_ITALIAN},    {"ja", LANG_JAPANESE},
    {"ka", LANG_GEORGIAN},   {"kk", LANG_KAZAK},      {"kn", LANG_KANNADA},    {"ko", LANG_KOREAN},
    {"lt", LANG_LITHUANIAN}, {"lv", LANG_LATVIAN},    {"mk", LANG_MACEDONIAN}, {"ml", LANG_MALAYALAM},
    {"mn", LANG_MONGOLIAN},  {"mr", LANG_MARATHI},    {"ms", LANG_MALAY},      {"nb", LANG_NORWEGIAN},
    {"nl", LANG_DUTCH},      {"nn", LANG_NORWEGIAN},  {"no", LANG_NORWEGIAN},  {"pa", LANG_PUNJABI},
    {"pl", LANG_POLISH},     {"pt", LANG_PORTUGUESE}, {"ro", LANG_ROMANIAN},   {"ru", LANG_RUSSIAN},
    {"sk", LANG_SLOVAK},     {"sl", LANG_SLOVENIAN},  {"sq", LANG_ALBANIAN},   {"sr", LANG_SERBIAN},
    {"sv", LANG_SWEDISH},    {"sw", LANG_SWAHILI},    {"ta", LANG_TAMIL},      {"te", LANG_TELUGU},
    {"th", LANG_THAI},       {"tr", LANG_TURKISH},    {"uk", LANG_UKRAINIAN},  {"ur", LANG_URDU},
    {"vi", LANG_VIETNAMESE}, {"zh", LANG_CHINESE},    {"zu", LANG_ZULU},
};

// Lowercase ASCII only; a BCP-47 primary subtag is ASCII by definition.
[[nodiscard]] char ToLowerAscii(char InCharacter) noexcept
{
    return InCharacter >= 'A' && InCharacter <= 'Z' ? static_cast<char>(InCharacter - 'A' + 'a') : InCharacter;
}

// Extracts the primary language subtag of a BCP-47 tag into a small buffer:
void ParsePrimaryLanguage(const char* InTag, char* OutCode, std::size_t InBytes) noexcept
{
    if (OutCode == nullptr || InBytes == 0)
    {
        return;
    }
    std::size_t ScopedIndex = 0;
    while (ScopedIndex + 1 < InBytes && InTag[ScopedIndex] != '\0' && InTag[ScopedIndex] != '-' &&
           InTag[ScopedIndex] != '_')
    {
        OutCode[ScopedIndex] = ToLowerAscii(InTag[ScopedIndex]);
        ++ScopedIndex;
    }
    OutCode[ScopedIndex] = '\0';
}

[[nodiscard]] WORD PrimaryLanguageFromCode(const char* InCode) noexcept
{
    for (const LanguageCode& ScopedEntry : kLanguageCodes)
    {
        if (std::strcmp(ScopedEntry.code, InCode) == 0)
        {
            return ScopedEntry.primary;
        }
    }
    return 0;
}

// Locale name resolution on top of the table: gives the region when the tag has
[[nodiscard]] LCID LcidFromBcp47(const char* InTag) noexcept
{
    wchar_t ScopedWide[kMaxLanguageTagBytes]{};
    if (MultiByteToWideChar(CP_UTF8, 0, InTag, -1, ScopedWide, static_cast<int>(std::size(ScopedWide))) <= 0)
    {
        return 0;
    }
    return LocaleNameToLCID(ScopedWide, 0);
}

// SAPI exposes the voice category through ISpObjectTokenCategory. sphelper.h's
[[nodiscard]] ISpObjectTokenCategory* OpenVoiceCategory() noexcept
{
    ISpObjectTokenCategory* ScopedCategory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
                                reinterpret_cast<void**>(&ScopedCategory))) ||
        ScopedCategory == nullptr)
    {
        return nullptr;
    }
    if (FAILED(ScopedCategory->SetId(SPCAT_VOICES, FALSE)))
    {
        ScopedCategory->Release();
        return nullptr;
    }
    return ScopedCategory;
}

// A token's Language attribute is a hexadecimal LCID string ("409", "419",
[[nodiscard]] bool ReadTokenLanguage(ISpObjectToken* InToken, LCID* OutLcid) noexcept
{
    if (InToken == nullptr || OutLcid == nullptr)
    {
        return false;
    }
    LPWSTR ScopedValue = nullptr;
    if (FAILED(InToken->GetStringValue(L"Language", &ScopedValue)) || ScopedValue == nullptr)
    {
        return false;
    }
    wchar_t* ScopedEnd = nullptr;
    const unsigned long ScopedLcid = std::wcstoul(ScopedValue, &ScopedEnd, 16);
    const bool ScopedOk = ScopedEnd != ScopedValue;
    CoTaskMemFree(ScopedValue);
    if (!ScopedOk)
    {
        return false;
    }
    *OutLcid = static_cast<LCID>(ScopedLcid);
    return true;
}

// The system default voice, materialized from the category's default token id.
[[nodiscard]] ISpObjectToken* OpenDefaultVoiceToken(ISpObjectTokenCategory* InCategory, LCID* OutLcid) noexcept
{
    if (InCategory == nullptr)
    {
        return nullptr;
    }
    LPWSTR ScopedTokenId = nullptr;
    if (FAILED(InCategory->GetDefaultTokenId(&ScopedTokenId)) || ScopedTokenId == nullptr)
    {
        return nullptr;
    }
    ISpObjectToken* ScopedToken = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_ISpObjectToken,
                                   reinterpret_cast<void**>(&ScopedToken))) &&
        ScopedToken != nullptr)
    {
        if (FAILED(ScopedToken->SetId(SPCAT_VOICES, ScopedTokenId, FALSE)))
        {
            ScopedToken->Release();
            ScopedToken = nullptr;
        }
        else if (OutLcid != nullptr)
        {
            (void)ReadTokenLanguage(ScopedToken, OutLcid);
        }
    }
    CoTaskMemFree(ScopedTokenId);
    return ScopedToken;
}

// 4 = exact locale, 3 = same language and region, 2 = same language, 0 = no.
[[nodiscard]] std::uint32_t ScoreVoice(LCID InVoice, WORD InRequestedPrimary, LCID InRequestedLcid) noexcept
{
    if (InVoice == 0)
    {
        return 0;
    }
    if (InRequestedLcid != 0 && InVoice == InRequestedLcid)
    {
        return 4;
    }
    if (InRequestedPrimary == 0 || PRIMARYLANGID(InVoice) != InRequestedPrimary)
    {
        return 0;
    }
    if (InRequestedLcid != 0 && SUBLANGID(InVoice) == SUBLANGID(InRequestedLcid))
    {
        return 3;
    }
    return 2;
}
}
#endif

SapiTts::~SapiTts()
{
    Shutdown();
}

bool SapiTts::Initialize() noexcept
{
#if defined(_WIN32)
    // Creating the ISpVoice initializes the calling thread's COM apartment. A
    if (!RequireMainThread("SapiTts::Initialize"))
    {
        return false;
    }
    if (MVoice != nullptr)
    {
        return true;
    }
    const HRESULT ScopedComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(ScopedComResult))
    {
        MComInitialized = true;
    }
    else if (ScopedComResult != RPC_E_CHANGED_MODE)
    {
        LogError("tts", "SAPI: CoInitializeEx failed (HRESULT 0x{:08X})", static_cast<std::uint32_t>(ScopedComResult));
        return false;
    }
    ISpVoice* ScopedVoice = nullptr;
    if (FAILED(
            CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&ScopedVoice))))
    {
        LogError("tts", "SAPI: CoCreateInstance(CLSID_SpVoice) failed; native speech is unavailable");
        return false;
    }
    MVoice = ScopedVoice;
    MThreadId = static_cast<std::uint32_t>(::GetCurrentThreadId());
    LogInfo("tts", "SAPI 5 voice ready on the main thread (thread {})", MThreadId);
    return true;
#else
    return false;
#endif
}

void SapiTts::Shutdown() noexcept
{
#if defined(_WIN32)
    if (MVoice != nullptr)
    {
        Voice(MVoice)->Release();
        MVoice = nullptr;
    }
    if (MComInitialized)
    {
        CoUninitialize();
        MComInitialized = false;
    }
#endif
}

bool SapiTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
#if defined(_WIN32)
    if (MVoice == nullptr || InUtf8.empty())
    {
        return false;
    }
    if (!RequireMainThread("SapiTts::Speak"))
    {
        return false;
    }
    if (MThreadId != 0 && static_cast<std::uint32_t>(::GetCurrentThreadId()) != MThreadId)
    {
        LogError("tts", "SAPI: Speak from a different thread than Initialize; the utterance was dropped");
        return false;
    }
    std::array<wchar_t, 512> ScopedWide{};
    const int ScopedSourceLength = static_cast<int>(std::min<std::size_t>(InUtf8.size(), ScopedWide.size() - 1));
    if (InUtf8.size() >= ScopedWide.size())
    {
        // The old path cut the text mid-sentence without a trace. Keep the hard
        LogWarn("tts", "SAPI: utterance is {} bytes, speaking the first {}; the remainder was dropped",
                InUtf8.size(), ScopedSourceLength);
    }
    const int ScopedCount = MultiByteToWideChar(CP_UTF8, 0, InUtf8.data(), ScopedSourceLength, ScopedWide.data(),
                                                static_cast<int>(ScopedWide.size() - 1));
    if (ScopedCount <= 0)
    {
        return false;
    }
    ScopedWide[static_cast<std::size_t>(ScopedCount)] = L'\0';
    DWORD ScopedFlags = SPF_ASYNC;
    if (InPriority == TtsPriority::Interrupt)
    {
        ScopedFlags |= SPF_PURGEBEFORESPEAK;
    }
    return SUCCEEDED(Voice(MVoice)->Speak(ScopedWide.data(), ScopedFlags, nullptr));
#else
    (void)InUtf8;
    (void)InPriority;
    return false;
#endif
}

void SapiTts::Stop() noexcept
{
#if defined(_WIN32)
    if (MVoice == nullptr || !RequireMainThread("SapiTts::Stop"))
    {
        return;
    }
    (void)Voice(MVoice)->Speak(nullptr, SPF_PURGEBEFORESPEAK | SPF_ASYNC, nullptr);
#endif
}

void SapiTts::SetRate(float InRate) noexcept
{
#if defined(_WIN32)
    if (MVoice == nullptr || !RequireMainThread("SapiTts::SetRate"))
    {
        return;
    }
    const long ScopedSapiRate = static_cast<long>(std::clamp(InRate, -10.0f, 10.0f));
    (void)Voice(MVoice)->SetRate(ScopedSapiRate);
#else
    (void)InRate;
#endif
}

void SapiTts::SetVoice(const char* InLanguageBCP47) noexcept
{
#if defined(_WIN32)
    if (MVoice == nullptr)
    {
        return;
    }
    if (!RequireMainThread("SapiTts::SetVoice"))
    {
        return;
    }
    if (MThreadId != 0 && static_cast<std::uint32_t>(::GetCurrentThreadId()) != MThreadId)
    {
        LogError("tts", "SAPI: SetVoice from a different thread than Initialize; the request was dropped");
        return;
    }
    if (InLanguageBCP47 == nullptr || InLanguageBCP47[0] == '\0')
    {
        return;
    }

    char ScopedLanguage[8]{};
    ParsePrimaryLanguage(InLanguageBCP47, ScopedLanguage, sizeof(ScopedLanguage));
    WORD ScopedRequestedPrimary = PrimaryLanguageFromCode(ScopedLanguage);
    const LCID ScopedRequestedLcid = LcidFromBcp47(InLanguageBCP47);
    if (ScopedRequestedPrimary == 0 && ScopedRequestedLcid != 0)
    {
        ScopedRequestedPrimary = PRIMARYLANGID(ScopedRequestedLcid);
    }
    if (ScopedRequestedPrimary == 0 && ScopedRequestedLcid == 0)
    {
        LogWarn("tts", "SAPI: '{}' is not a recognizable BCP-47 tag; keeping the current voice", InLanguageBCP47);
        return;
    }

    // The system default is both the tie-breaker and the last-resort fallback.
    ISpObjectTokenCategory* ScopedCategory = OpenVoiceCategory();
    LCID ScopedDefaultLcid = 0;
    ISpObjectToken* ScopedDefaultToken = OpenDefaultVoiceToken(ScopedCategory, &ScopedDefaultLcid);

    IEnumSpObjectTokens* ScopedEnumerator = nullptr;
    if (ScopedCategory == nullptr ||
        FAILED(ScopedCategory->EnumTokens(nullptr, nullptr, &ScopedEnumerator)) || ScopedEnumerator == nullptr)
    {
        if (ScopedDefaultToken != nullptr)
        {
            (void)Voice(MVoice)->SetVoice(ScopedDefaultToken);
        }
        LogWarn("tts", "SAPI: the voice category could not be enumerated; kept the current voice");
        if (ScopedDefaultToken != nullptr)
        {
            ScopedDefaultToken->Release();
        }
        if (ScopedCategory != nullptr)
        {
            ScopedCategory->Release();
        }
        return;
    }

    // Enumerate every installed voice token. The fixed arrays keep this path
    std::array<ISpObjectToken*, kMaxVoiceTokens> ScopedTokens{};
    std::array<LCID, kMaxVoiceTokens> ScopedLcids{};
    std::uint32_t ScopedTokenCount = 0;
    while (ScopedTokenCount < ScopedTokens.size())
    {
        ISpObjectToken* ScopedToken = nullptr;
        ULONG ScopedFetched = 0;
        if (ScopedEnumerator->Next(1, &ScopedToken, &ScopedFetched) != S_OK || ScopedToken == nullptr ||
            ScopedFetched == 0)
        {
            break;
        }
        LCID ScopedLcid = 0;
        (void)ReadTokenLanguage(ScopedToken, &ScopedLcid);
        ScopedTokens[ScopedTokenCount] = ScopedToken;
        ScopedLcids[ScopedTokenCount] = ScopedLcid;
        ++ScopedTokenCount;
    }
    ScopedEnumerator->Release();
    ScopedCategory->Release();

    // 2x the locale score, +1 for the system default: an exact match always
    std::uint32_t ScopedBest = 0;
    std::uint32_t ScopedBestWeight = 0;
    std::uint32_t ScopedBestScore = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedTokenCount; ++ScopedIndex)
    {
        const std::uint32_t ScopedScore =
            ScoreVoice(ScopedLcids[ScopedIndex], ScopedRequestedPrimary, ScopedRequestedLcid);
        const bool ScopedIsDefault = ScopedDefaultLcid != 0 && ScopedLcids[ScopedIndex] == ScopedDefaultLcid;
        const std::uint32_t ScopedWeight = ScopedScore * 2u + (ScopedIsDefault ? 1u : 0u);
        if (ScopedWeight > ScopedBestWeight)
        {
            ScopedBestWeight = ScopedWeight;
            ScopedBestScore = ScopedScore;
            ScopedBest = ScopedIndex;
        }
    }

    if (ScopedTokenCount == 0)
    {
        if (ScopedDefaultToken != nullptr)
        {
            (void)Voice(MVoice)->SetVoice(ScopedDefaultToken);
            LogWarn("tts", "SAPI: no voices enumerated; installed the default voice (LCID 0x{:04X})",
                    static_cast<std::uint32_t>(ScopedDefaultLcid));
        }
        else
        {
            LogError("tts", "SAPI: no voices are installed; speech stays on the previous voice");
        }
        if (ScopedDefaultToken != nullptr)
        {
            ScopedDefaultToken->Release();
        }
        return;
    }

    if (ScopedBestScore == 0)
    {
        // No voice shares the requested language. Fall back to the system
        std::uint32_t ScopedFallback = ScopedBest;
        bool ScopedHasDefault = false;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedTokenCount; ++ScopedIndex)
        {
            if (ScopedDefaultLcid != 0 && ScopedLcids[ScopedIndex] == ScopedDefaultLcid)
            {
                ScopedFallback = ScopedIndex;
                ScopedHasDefault = true;
                break;
            }
        }
        if (ScopedHasDefault)
        {
            LogWarn("tts",
                    "SAPI: no installed voice matches '{}' (LCID 0x{:04X}); falling back to the system default "
                    "LCID 0x{:04X}",
                    InLanguageBCP47, static_cast<std::uint32_t>(ScopedRequestedLcid),
                    static_cast<std::uint32_t>(ScopedLcids[ScopedFallback]));
        }
        else
        {
            // A machine can have voices installed without a default token id.
            LogWarn("tts",
                    "SAPI: no installed voice matches '{}' (LCID 0x{:04X}) and no system default is registered; "
                    "using the first installed voice LCID 0x{:04X}",
                    InLanguageBCP47, static_cast<std::uint32_t>(ScopedRequestedLcid),
                    static_cast<std::uint32_t>(ScopedLcids[ScopedFallback]));
        }
        ScopedBest = ScopedFallback;
        ScopedBestScore = 0;
    }

    const HRESULT ScopedSetResult = Voice(MVoice)->SetVoice(ScopedTokens[ScopedBest]);
    if (FAILED(ScopedSetResult))
    {
        LogWarn("tts", "SAPI: SetVoice rejected LCID 0x{:04X} (HRESULT 0x{:08X}); kept the current voice",
                static_cast<std::uint32_t>(ScopedLcids[ScopedBest]), static_cast<std::uint32_t>(ScopedSetResult));
    }
    else
    {
        LogInfo("tts", "SAPI voice for '{}': LCID 0x{:04X} ({} of {} installed, match score {})", InLanguageBCP47,
                static_cast<std::uint32_t>(ScopedLcids[ScopedBest]), ScopedBest + 1u, ScopedTokenCount,
                ScopedBestScore);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedTokenCount; ++ScopedIndex)
    {
        if (ScopedIndex != ScopedBest)
        {
            ScopedTokens[ScopedIndex]->Release();
        }
    }
    // ISpVoice::SetVoice AddRefs the token, so the caller's reference can go.
    ScopedTokens[ScopedBest]->Release();
    if (ScopedDefaultToken != nullptr)
    {
        ScopedDefaultToken->Release();
    }
#else
    (void)InLanguageBCP47;
#endif
}

TtsBackend SapiTts::Backend() noexcept
{
    TtsBackend ScopedBackend;
    ScopedBackend.user = this;
    ScopedBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    { return static_cast<SapiTts*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority); };
    ScopedBackend.stop = +[](void* InUser) noexcept { static_cast<SapiTts*>(InUser)->Stop(); };
    ScopedBackend.setRate =
        +[](void* InUser, float InRate) noexcept { static_cast<SapiTts*>(InUser)->SetRate(InRate); };
    ScopedBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    { static_cast<SapiTts*>(InUser)->SetVoice(InLanguage); };
    return ScopedBackend;
}
}
