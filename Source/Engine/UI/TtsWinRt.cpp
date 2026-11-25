// FemboyEngine2D - Windows one-core speech (WinRT) and the composite TTS backend.
//
// Desktop SAPI voices and the modern speech stack expose different voices: SAPI
// cannot see the OneCore voices and the WinRT synthesizer cannot see the desktop
// ones. The engine therefore asks both and routes by the selected voice id. The
// WinRT projection needs exceptions enabled, so this translation unit is the one
// place in the engine compiled with them.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#if defined(_WIN32)

#if defined(FBY_HAS_WINRT)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{
#if defined(FBY_HAS_WINRT)
// Case-insensitive language score identical in spirit to the SAPI one:
// 3 = the exact tag, 2 = the same primary language, 0 = a different language.
[[nodiscard]] std::uint32_t ScoreLanguage(const char* InInstalled, const char* InRequested) noexcept
{
    if (InInstalled == nullptr || InRequested == nullptr)
    {
        return 0;
    }
    const auto ScopedLower = [](char InCharacter) noexcept
    {
        return static_cast<char>(InCharacter >= 'A' && InCharacter <= 'Z' ? InCharacter - 'A' + 'a' : InCharacter);
    };
    std::size_t ScopedIndex = 0;
    while (InInstalled[ScopedIndex] != '\0' && InRequested[ScopedIndex] != '\0')
    {
        if (ScopedLower(InInstalled[ScopedIndex]) != ScopedLower(InRequested[ScopedIndex]))
        {
            return 0;
        }
        ++ScopedIndex;
    }
    if (InInstalled[ScopedIndex] == '\0' && InRequested[ScopedIndex] == '\0')
    {
        return 3;
    }
    const char ScopedRemaining = InInstalled[ScopedIndex] == '\0' ? InRequested[ScopedIndex] : InInstalled[ScopedIndex];
    return ScopedRemaining == '-' ? 2 : 0;
}

void CopyWide(const wchar_t* InWide, char* OutText, std::size_t InBytes) noexcept
{
    if (OutText == nullptr || InBytes == 0)
    {
        return;
    }
    OutText[0] = '\0';
    if (InWide == nullptr)
    {
        return;
    }
    (void)WideCharToMultiByte(CP_UTF8, 0, InWide, -1, OutText, static_cast<int>(InBytes), nullptr, nullptr);
    OutText[InBytes - 1] = '\0';
}

struct WinRtTtsImpl
{
    winrt::Windows::Media::SpeechSynthesis::SpeechSynthesizer Synthesizer{nullptr};
    winrt::Windows::Media::Playback::MediaPlayer Player{nullptr};
    winrt::hstring VoiceId{};
    float Rate = 0.0f;
};
#endif
}

WinRtTts::~WinRtTts()
{
    Shutdown();
}

bool WinRtTts::Initialize() noexcept
{
#if defined(FBY_HAS_WINRT)
    if (!RequireMainThread("WinRtTts::Initialize"))
    {
        return false;
    }
    if (MReady)
    {
        return true;
    }
    // SAPI usually initialized the thread as an STA already; asking WinRT for
    // the same apartment is then a no-op. A worker-created MTA is the fallback.
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (...)
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
            LogWarn("tts", "WinRT: the COM apartment could not be initialized; one-core voices stay off");
            return false;
        }
    }
    try
    {
        auto* ScopedImpl = new (std::nothrow) WinRtTtsImpl();
        ScopedImpl->Synthesizer = winrt::Windows::Media::SpeechSynthesis::SpeechSynthesizer();
        ScopedImpl->Player = winrt::Windows::Media::Playback::MediaPlayer();
        ScopedImpl->Player.AutoPlay(false);
        MImpl = ScopedImpl;
        MBackend.user = this;
        MBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
        {
            return static_cast<WinRtTts*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority);
        };
        MBackend.stop = +[](void* InUser) noexcept
        {
            static_cast<WinRtTts*>(InUser)->Stop();
        };
        MBackend.setRate = +[](void* InUser, float InRate) noexcept
        {
            static_cast<WinRtTts*>(InUser)->SetRate(InRate);
        };
        MBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
        {
            static_cast<WinRtTts*>(InUser)->SetVoice(InLanguage);
        };
        MBackend.setVoiceById = +[](void* InUser, const char* InId) noexcept
        {
            return static_cast<WinRtTts*>(InUser)->SetVoiceById(InId);
        };
        MBackend.enumerateVoices = +[](void* InUser, TtsVoiceInfo* InVoices, std::uint32_t InCapacity) noexcept
        {
            return static_cast<WinRtTts*>(InUser)->EnumerateVoices(InVoices, InCapacity);
        };
        MReady = true;
        LogInfo("tts", "WinRT speech synthesizer ready ({} one-core voices)",
                static_cast<unsigned>(ScopedImpl->Synthesizer.AllVoices().Size()));
        return true;
    }
    catch (winrt::hresult_error const& InError)
    {
        LogWarn("tts", "WinRT: SpeechSynthesizer creation failed (0x{:08X}): {}",
                static_cast<std::uint32_t>(InError.code().value), winrt::to_string(InError.message()));
    }
    catch (...)
    {
        LogWarn("tts", "WinRT: SpeechSynthesizer creation failed");
    }
    return false;
#else
    return false;
#endif
}

void WinRtTts::Shutdown() noexcept
{
#if defined(FBY_HAS_WINRT)
    if (MImpl == nullptr)
    {
        return;
    }
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    try
    {
        ScopedImpl->Player.Pause();
        ScopedImpl->Player.Source(nullptr);
    }
    catch (...)
    {
    }
    delete ScopedImpl;
    MImpl = nullptr;
    MReady = false;
#endif
}

bool WinRtTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
#if defined(FBY_HAS_WINRT)
    (void)InPriority;
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (!MReady || ScopedImpl == nullptr || InUtf8.empty() || !RequireMainThread("WinRtTts::Speak"))
    {
        return false;
    }
    try
    {
        // Synthesis is a few milliseconds for a sentence; the async operation is
        // drained synchronously because the narrator already owns the frame
        // pacing of the speech queue.
        winrt::Windows::Media::SpeechSynthesis::SpeechSynthesisStream ScopedStream =
            ScopedImpl->Synthesizer.SynthesizeTextToStreamAsync(winrt::to_hstring(InUtf8)).get();
        ScopedImpl->Player.Source(
            winrt::Windows::Media::Core::MediaSource::CreateFromStream(ScopedStream, ScopedStream.ContentType()));
        ScopedImpl->Player.Play();
        return true;
    }
    catch (winrt::hresult_error const& InError)
    {
        LogWarn("tts", "WinRT: synthesis failed (0x{:08X})", static_cast<std::uint32_t>(InError.code().value));
    }
    catch (...)
    {
        LogWarn("tts", "WinRT: synthesis failed");
    }
    return false;
#else
    (void)InUtf8;
    (void)InPriority;
    return false;
#endif
}

void WinRtTts::Stop() noexcept
{
#if defined(FBY_HAS_WINRT)
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (!MReady || ScopedImpl == nullptr || !RequireMainThread("WinRtTts::Stop"))
    {
        return;
    }
    try
    {
        ScopedImpl->Player.Pause();
        ScopedImpl->Player.Source(nullptr);
    }
    catch (...)
    {
    }
#endif
}

void WinRtTts::SetRate(float InRate) noexcept
{
#if defined(FBY_HAS_WINRT)
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (ScopedImpl == nullptr)
    {
        return;
    }
    // TtsBackend uses the SAPI convention (-10..10, 0 normal); the WinRT rate is
    // a multiplier with 1.0 as normal.
    ScopedImpl->Rate = std::clamp(1.0f + InRate * 0.1f, 0.5f, 6.0f);
    try
    {
        ScopedImpl->Synthesizer.Options().SpeakingRate(ScopedImpl->Rate);
    }
    catch (...)
    {
    }
#else
    (void)InRate;
#endif
}

void WinRtTts::SetVoice(const char* InLanguageBCP47) noexcept
{
#if defined(FBY_HAS_WINRT)
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (!MReady || ScopedImpl == nullptr || !RequireMainThread("WinRtTts::SetVoice") || InLanguageBCP47 == nullptr ||
        InLanguageBCP47[0] == '\0')
    {
        return;
    }
    try
    {
        std::uint32_t ScopedBestScore = 0;
        winrt::Windows::Media::SpeechSynthesis::VoiceInformation ScopedBest{nullptr};
        for (const auto& ScopedVoice : ScopedImpl->Synthesizer.AllVoices())
        {
            char ScopedLanguage[16]{};
            CopyWide(ScopedVoice.Language().c_str(), ScopedLanguage, sizeof(ScopedLanguage));
            const std::uint32_t ScopedScore = ScoreLanguage(ScopedLanguage, InLanguageBCP47);
            if (ScopedScore > ScopedBestScore)
            {
                ScopedBestScore = ScopedScore;
                ScopedBest = ScopedVoice;
            }
        }
        if (ScopedBestScore > 0 && ScopedBest != nullptr)
        {
            ScopedImpl->Synthesizer.Voice(ScopedBest);
            ScopedImpl->VoiceId = ScopedBest.Id();
            LogInfo("tts", "WinRT voice for '{}' (score {})", InLanguageBCP47, ScopedBestScore);
            return;
        }
        LogWarn("tts", "WinRT: no one-core voice matches '{}'; the system default stays active", InLanguageBCP47);
    }
    catch (...)
    {
        LogWarn("tts", "WinRT: SetVoice failed");
    }
#else
    (void)InLanguageBCP47;
#endif
}

bool WinRtTts::SetVoiceById(const char* InId) noexcept
{
#if defined(FBY_HAS_WINRT)
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (!MReady || ScopedImpl == nullptr || !RequireMainThread("WinRtTts::SetVoiceById") || InId == nullptr ||
        InId[0] == '\0')
    {
        return false;
    }
    try
    {
        const winrt::hstring ScopedWanted = winrt::to_hstring(InId);
        for (const auto& ScopedVoice : ScopedImpl->Synthesizer.AllVoices())
        {
            if (ScopedVoice.Id() == ScopedWanted)
            {
                ScopedImpl->Synthesizer.Voice(ScopedVoice);
                ScopedImpl->VoiceId = ScopedWanted;
                LogInfo("tts", "WinRT voice selected by id");
                return true;
            }
        }
    }
    catch (...)
    {
    }
    return false;
#else
    (void)InId;
    return false;
#endif
}

std::uint32_t WinRtTts::EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
#if defined(FBY_HAS_WINRT)
    auto* ScopedImpl = static_cast<WinRtTtsImpl*>(MImpl);
    if (!MReady || ScopedImpl == nullptr || OutVoices == nullptr || InCapacity == 0 ||
        !RequireMainThread("WinRtTts::EnumerateVoices"))
    {
        return 0;
    }
    try
    {
        std::uint32_t ScopedCount = 0;
        for (const auto& ScopedVoice : ScopedImpl->Synthesizer.AllVoices())
        {
            if (ScopedCount >= InCapacity)
            {
                break;
            }
            TtsVoiceInfo& ScopedInfo = OutVoices[ScopedCount];
            ScopedInfo = TtsVoiceInfo{};
            CopyWide(ScopedVoice.Id().c_str(), ScopedInfo.Id, sizeof(ScopedInfo.Id));
            CopyWide(ScopedVoice.DisplayName().c_str(), ScopedInfo.Name, sizeof(ScopedInfo.Name));
            CopyWide(ScopedVoice.Language().c_str(), ScopedInfo.Language, sizeof(ScopedInfo.Language));
            if (ScopedVoice.Gender() == winrt::Windows::Media::SpeechSynthesis::VoiceGender::Male)
            {
                ScopedInfo.Gender = 1;
            }
            else if (ScopedVoice.Gender() == winrt::Windows::Media::SpeechSynthesis::VoiceGender::Female)
            {
                ScopedInfo.Gender = 2;
            }
            ++ScopedCount;
        }
        return ScopedCount;
    }
    catch (...)
    {
        return 0;
    }
#else
    (void)OutVoices;
    (void)InCapacity;
    return 0;
#endif
}

TtsBackend WinRtTts::Backend() noexcept
{
    return MBackend;
}

CompositeTts::~CompositeTts()
{
    Shutdown();
}

bool CompositeTts::Initialize() noexcept
{
    if (!RequireMainThread("CompositeTts::Initialize"))
    {
        return false;
    }
    (void)MWinRt.Initialize();
    const bool ScopedSapiReady = MSapi.Initialize();
    MInitialized = ScopedSapiReady || MWinRt.IsReady();
    MBackend.user = this;
    MBackend.speak = +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    {
        return static_cast<CompositeTts*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority);
    };
    MBackend.stop = +[](void* InUser) noexcept
    {
        static_cast<CompositeTts*>(InUser)->Stop();
    };
    MBackend.setRate = +[](void* InUser, float InRate) noexcept
    {
        static_cast<CompositeTts*>(InUser)->SetRate(InRate);
    };
    MBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    {
        static_cast<CompositeTts*>(InUser)->SetVoice(InLanguage);
    };
    MBackend.setVoiceById = +[](void* InUser, const char* InId) noexcept
    {
        return static_cast<CompositeTts*>(InUser)->SetVoiceById(InId);
    };
    MBackend.enumerateVoices = +[](void* InUser, TtsVoiceInfo* InVoices, std::uint32_t InCapacity) noexcept
    {
        return static_cast<CompositeTts*>(InUser)->EnumerateVoices(InVoices, InCapacity);
    };
    return MInitialized;
}

void CompositeTts::Shutdown() noexcept
{
    MWinRt.Shutdown();
    MSapi.Shutdown();
    MInitialized = false;
}

bool CompositeTts::IsReady() const noexcept
{
    return MInitialized;
}

bool CompositeTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
    // The selected route is a preference, not a hard constraint: a machine can
    // have a working one-core voice and a broken SAPI install (or the other way
    // around), and silence is the worst outcome for an accessibility feature.
    if (MRoute == Route::WinRt)
    {
        if (MWinRt.IsReady() && MWinRt.Speak(InUtf8, InPriority))
        {
            return true;
        }
        return MSapi.Speak(InUtf8, InPriority);
    }
    if (MSapi.Speak(InUtf8, InPriority))
    {
        return true;
    }
    return MWinRt.IsReady() && MWinRt.Speak(InUtf8, InPriority);
}

void CompositeTts::Stop() noexcept
{
    MWinRt.Stop();
    MSapi.Stop();
}

void CompositeTts::SetRate(float InRate) noexcept
{
    MSapi.SetRate(InRate);
    MWinRt.SetRate(InRate);
}

void CompositeTts::SetVoice(const char* InLanguageBCP47) noexcept
{
    if (!MWinRt.IsReady())
    {
        MRoute = Route::Sapi;
        MSapi.SetVoice(InLanguageBCP47);
        return;
    }
    // Prefer a desktop voice that shares the language; fall back to a one-core
    // voice when SAPI has no match; otherwise keep the SAPI default behaviour.
    std::array<TtsVoiceInfo, kMaxTtsVoices> ScopedSapiVoices{};
    const std::uint32_t ScopedSapiCount = MSapi.EnumerateVoices(ScopedSapiVoices.data(), kMaxTtsVoices);
    std::uint32_t ScopedSapiScore = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedSapiCount; ++ScopedIndex)
    {
        ScopedSapiScore =
            std::max(ScopedSapiScore, ScoreLanguage(ScopedSapiVoices[ScopedIndex].Language, InLanguageBCP47));
    }
    std::array<TtsVoiceInfo, kMaxTtsVoices> ScopedWinRtVoices{};
    const std::uint32_t ScopedWinRtCount = MWinRt.EnumerateVoices(ScopedWinRtVoices.data(), kMaxTtsVoices);
    std::uint32_t ScopedWinRtScore = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWinRtCount; ++ScopedIndex)
    {
        ScopedWinRtScore =
            std::max(ScopedWinRtScore, ScoreLanguage(ScopedWinRtVoices[ScopedIndex].Language, InLanguageBCP47));
    }
    if (ScopedWinRtScore > ScopedSapiScore)
    {
        MRoute = Route::WinRt;
        MWinRt.SetVoice(InLanguageBCP47);
        return;
    }
    MRoute = Route::Sapi;
    MSapi.SetVoice(InLanguageBCP47);
}

bool CompositeTts::SetVoiceById(const char* InId) noexcept
{
    if (InId == nullptr || InId[0] == '\0')
    {
        return false;
    }
    constexpr const char kWinRtPrefix[] = "winrt:";
    if (std::strncmp(InId, kWinRtPrefix, sizeof(kWinRtPrefix) - 1) == 0)
    {
        if (MWinRt.IsReady() && MWinRt.SetVoiceById(InId + sizeof(kWinRtPrefix) - 1))
        {
            MRoute = Route::WinRt;
            return true;
        }
        return false;
    }
    if (MSapi.SetVoiceById(InId))
    {
        MRoute = Route::Sapi;
        return true;
    }
    // A bare id from an older project file: the one-core voices are the only
    // remaining source.
    if (MWinRt.IsReady() && MWinRt.SetVoiceById(InId))
    {
        MRoute = Route::WinRt;
        return true;
    }
    return false;
}

std::uint32_t CompositeTts::EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    if (OutVoices == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::uint32_t ScopedTotal = MSapi.EnumerateVoices(OutVoices, InCapacity);
    if (ScopedTotal >= InCapacity || !MWinRt.IsReady())
    {
        return ScopedTotal;
    }
    std::array<TtsVoiceInfo, kMaxTtsVoices> ScopedWinRtVoices{};
    const std::uint32_t ScopedWinRtCount = MWinRt.EnumerateVoices(ScopedWinRtVoices.data(), kMaxTtsVoices);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedWinRtCount && ScopedTotal < InCapacity; ++ScopedIndex)
    {
        TtsVoiceInfo& ScopedTarget = OutVoices[ScopedTotal];
        ScopedTarget = ScopedWinRtVoices[ScopedIndex];
        char ScopedPrefixed[kTtsVoiceIdBytes]{};
        std::snprintf(ScopedPrefixed, sizeof(ScopedPrefixed), "winrt:%s", ScopedWinRtVoices[ScopedIndex].Id);
        std::memcpy(ScopedTarget.Id, ScopedPrefixed, sizeof(ScopedTarget.Id) - 1);
        ScopedTarget.Id[sizeof(ScopedTarget.Id) - 1] = '\0';
        ++ScopedTotal;
    }
    return ScopedTotal;
}

TtsBackend CompositeTts::Backend() noexcept
{
    return MBackend;
}
}
#endif
