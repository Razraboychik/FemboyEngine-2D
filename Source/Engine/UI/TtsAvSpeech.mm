// FemboyEngine2D - macOS AVSpeechSynthesizer adapter.
//
// Compiled only on Apple targets (CMake adds this file and enables ARC). The
// public header stores the synthesizer as void*, so no AVFoundation type leaks
// into the engine include graph. Speech is asynchronous and queued by
// AVSpeechSynthesizer itself; Interrupt maps to an immediate boundary stop.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cstdint>

namespace Fby
{
namespace
{
[[nodiscard]] AVSpeechSynthesizer* Synthesizer(void* InRaw) noexcept
{
    return (__bridge AVSpeechSynthesizer*)InRaw;
}

[[nodiscard]] float MapRate(float InRate) noexcept
{
    // TtsBackend uses the SAPI convention (-10..10, 0 = normal). AVSpeech uses
    // 0..1 with 0.5 as the default rate.
    const float ScopedMapped = 0.5f + std::clamp(InRate, -10.0f, 10.0f) * 0.05f;
    return std::clamp(ScopedMapped, AVSpeechUtteranceMinimumSpeechRate, AVSpeechUtteranceMaximumSpeechRate);
}

[[nodiscard]] bool IsMainThread() noexcept
{
    return [NSThread isMainThread];
}

// BCP-47 comparison without NSString allocation per candidate:
//   3 = identical tag, 2 = same primary language ("ru" vs "ru-RU"),
//   0 = different language. A tag from another language family is never
//   installed; the system default is the explicit fallback instead.
[[nodiscard]] std::uint32_t ScoreLanguage(const char* InInstalled, const char* InRequested) noexcept
{
    if (InInstalled == nullptr || InRequested == nullptr)
    {
        return 0;
    }
    std::size_t ScopedIndex = 0;
    while (InInstalled[ScopedIndex] != '\0' && InRequested[ScopedIndex] != '\0')
    {
        const char ScopedA = InInstalled[ScopedIndex];
        const char ScopedB = InRequested[ScopedIndex];
        const auto ScopedLower = [](char InCharacter) noexcept
        {
            return static_cast<char>(InCharacter >= 'A' && InCharacter <= 'Z' ? InCharacter - 'A' + 'a' : InCharacter);
        };
        if (ScopedLower(ScopedA) != ScopedLower(ScopedB))
        {
            return 0;
        }
        if (ScopedA == '-')
        {
            return 2;
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

void CopyLanguage(const char* InSource, char* OutLanguage, std::size_t InBytes) noexcept
{
    if (OutLanguage == nullptr || InBytes == 0)
    {
        return;
    }
    std::size_t ScopedIndex = 0;
    if (InSource != nullptr)
    {
        while (ScopedIndex + 1 < InBytes && InSource[ScopedIndex] != '\0')
        {
            OutLanguage[ScopedIndex] = InSource[ScopedIndex];
            ++ScopedIndex;
        }
    }
    OutLanguage[ScopedIndex] = '\0';
}
}

AvSpeechTts::~AvSpeechTts()
{
    Shutdown();
}

bool AvSpeechTts::Initialize() noexcept
{
    if (!IsMainThread())
    {
        // AVSpeechSynthesizer is a main-run-loop object. Refuse rather than
        // creating it on a worker and corrupting the run loop contract.
        LogError("tts", "AVSpeech: Initialize called off the main run loop; speech is unavailable");
        FBY_ASSERT_MSG(false, "AvSpeechTts::Initialize() must run on the main run loop");
        return false;
    }
    if (MSynthesizer != nullptr)
    {
        return true;
    }
    @autoreleasepool
    {
        AVSpeechSynthesizer* ScopedSynthesizer = [[AVSpeechSynthesizer alloc] init];
        if (ScopedSynthesizer == nil)
        {
            LogError("tts", "AVSpeech: [[AVSpeechSynthesizer alloc] init] returned nil");
            return false;
        }
        // The engine owns one synthesizer for the process lifetime; bridging
        // retain keeps it alive while the C++ object stores only a void*.
        MSynthesizer = (void*)CFBridgingRetain(ScopedSynthesizer);
        LogInfo("tts", "AVSpeech synthesizer ready on the main run loop");
    }
    return MSynthesizer != nullptr;
}

void AvSpeechTts::Shutdown() noexcept
{
    if (MSynthesizer == nullptr)
    {
        return;
    }
    @autoreleasepool
    {
        AVSpeechSynthesizer* ScopedSynthesizer = (__bridge_transfer AVSpeechSynthesizer*)MSynthesizer;
        [ScopedSynthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        MSynthesizer = nullptr;
    }
}

bool AvSpeechTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
    if (MSynthesizer == nullptr || InUtf8.empty())
    {
        return false;
    }
    if (!IsMainThread())
    {
        LogError("tts", "AVSpeech: Speak called off the main run loop; the utterance was dropped");
        FBY_ASSERT_MSG(false, "AvSpeechTts::Speak() must run on the main run loop");
        return false;
    }
    @autoreleasepool
    {
        NSString* ScopedText = [[NSString alloc] initWithBytes:InUtf8.data()
                                                        length:InUtf8.size()
                                                      encoding:NSUTF8StringEncoding];
        if (ScopedText == nil)
        {
            return false;
        }
        AVSpeechUtterance* ScopedUtterance = [AVSpeechUtterance speechUtteranceWithString:ScopedText];
        ScopedUtterance.rate = MapRate(MRate);
        ScopedUtterance.volume = 1.0f;
        if (MLanguage[0] != '\0')
        {
            NSString* ScopedLanguage = [NSString stringWithUTF8String:MLanguage];
            AVSpeechSynthesisVoice* ScopedVoice = [AVSpeechSynthesisVoice voiceWithLanguage:ScopedLanguage];
            if (ScopedVoice != nil)
            {
                ScopedUtterance.voice = ScopedVoice;
            }
        }
        AVSpeechSynthesizer* ScopedSynthesizer = Synthesizer(MSynthesizer);
        if (InPriority == TtsPriority::Interrupt)
        {
            [ScopedSynthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        }
        [ScopedSynthesizer speakUtterance:ScopedUtterance];
    }
    return true;
}

void AvSpeechTts::Stop() noexcept
{
    if (MSynthesizer == nullptr || !IsMainThread())
    {
        return;
    }
    @autoreleasepool
    {
        [Synthesizer(MSynthesizer) stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
    }
}

void AvSpeechTts::SetRate(float InRate) noexcept
{
    MRate = std::clamp(InRate, -10.0f, 10.0f);
}

void AvSpeechTts::SetVoice(const char* InLanguageBCP47) noexcept
{
    MLanguage[0] = '\0';
    if (!IsMainThread())
    {
        LogError("tts", "AVSpeech: SetVoice called off the main run loop; the request was dropped");
        FBY_ASSERT_MSG(false, "AvSpeechTts::SetVoice() must run on the main run loop");
        return;
    }
    if (InLanguageBCP47 == nullptr || InLanguageBCP47[0] == '\0')
    {
        return;
    }
    // Enumerate every installed voice and install the best locale match. When the
    // requested language is not installed (a Russian narrative on an English
    // Mac), fall back to the system default language instead of going silent.
    @autoreleasepool
    {
        std::uint32_t ScopedBestScore = 0;
        AVSpeechSynthesisVoice* ScopedBest = nil;
        for (AVSpeechSynthesisVoice* ScopedVoice in [AVSpeechSynthesisVoice speechVoices])
        {
            const char* ScopedCode = ScopedVoice.language.UTF8String;
            const std::uint32_t ScopedScore = ScoreLanguage(ScopedCode, InLanguageBCP47);
            if (ScopedScore > ScopedBestScore)
            {
                ScopedBestScore = ScopedScore;
                ScopedBest = ScopedVoice;
            }
        }
        if (ScopedBest != nil)
        {
            CopyLanguage(ScopedBest.language.UTF8String, MLanguage, sizeof(MLanguage));
            LogInfo("tts", "AVSpeech voice for '{}': '{}' (match score {})", InLanguageBCP47, MLanguage,
                    ScopedBestScore);
            return;
        }
        NSString* ScopedFallback = [AVSpeechSynthesisVoice currentLanguageCode];
        if (ScopedFallback != nil)
        {
            CopyLanguage(ScopedFallback.UTF8String, MLanguage, sizeof(MLanguage));
            LogWarn("tts", "AVSpeech: no installed voice matches '{}'; falling back to the system default '{}'",
                    InLanguageBCP47, MLanguage);
            return;
        }
        LogError("tts", "AVSpeech: no voice matches '{}' and no system default is available", InLanguageBCP47);
    }
}

TtsBackend AvSpeechTts::Backend() noexcept
{
    TtsBackend ScopedBackend;
    ScopedBackend.user = this;
    ScopedBackend.speak =
        +[](void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept
    { return static_cast<AvSpeechTts*>(InUser)->Speak(std::string_view{InUtf8, InLength}, InPriority); };
    ScopedBackend.stop = +[](void* InUser) noexcept { static_cast<AvSpeechTts*>(InUser)->Stop(); };
    ScopedBackend.setRate =
        +[](void* InUser, float InRate) noexcept { static_cast<AvSpeechTts*>(InUser)->SetRate(InRate); };
    ScopedBackend.setVoice = +[](void* InUser, const char* InLanguage) noexcept
    { static_cast<AvSpeechTts*>(InUser)->SetVoice(InLanguage); };
    return ScopedBackend;
}
}
#endif
