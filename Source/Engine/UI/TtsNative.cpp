// FemboyEngine2D - platform TTS selection.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

namespace Fby
{
namespace
{
#if defined(_WIN32)
CompositeTts& CompositeInstance() noexcept
{
    static CompositeTts ScopedInstance;
    return ScopedInstance;
}
#elif defined(__APPLE__)
AvSpeechTts& AvSpeechInstance() noexcept
{
    static AvSpeechTts ScopedInstance;
    return ScopedInstance;
}
#elif defined(__linux__)
SpeechDispatcherTts& SpeechDispatcherInstance() noexcept
{
    static SpeechDispatcherTts ScopedInstance;
    return ScopedInstance;
}
#endif
}

const TtsBackend* NativeTtsBackend() noexcept
{
    // The first successful call constructs the OS synthesizer.
    if (!RequireMainThread("NativeTtsBackend"))
    {
        return nullptr;
    }
#if defined(_WIN32)
    static TtsBackend ScopedBackend;
    static bool ScopedInitialized = false;
    if (!ScopedInitialized && CompositeInstance().Initialize())
    {
        ScopedBackend = CompositeInstance().Backend();
        ScopedInitialized = true;
    }
    if (!ScopedInitialized)
    {
        LogWarn("tts",
                "native TTS (SAPI and one-core speech) is unavailable; BlindMode keeps the narrative log but "
                "cannot speak it");
    }
    return ScopedInitialized && ScopedBackend.speak != nullptr ? &ScopedBackend : nullptr;
#elif defined(__APPLE__)
    static TtsBackend ScopedBackend;
    static bool ScopedInitialized = false;
    if (!ScopedInitialized && AvSpeechInstance().Initialize())
    {
        ScopedBackend = AvSpeechInstance().Backend();
        ScopedInitialized = true;
    }
    if (!ScopedInitialized)
    {
        LogWarn("tts", "native TTS (AVSpeech) is unavailable; BlindMode keeps the narrative log but cannot speak it");
    }
    return ScopedInitialized && ScopedBackend.speak != nullptr ? &ScopedBackend : nullptr;
#elif defined(__linux__)
    static TtsBackend ScopedBackend;
    static bool ScopedInitialized = false;
    if (!ScopedInitialized && SpeechDispatcherInstance().Initialize())
    {
        ScopedBackend = SpeechDispatcherInstance().Backend();
        ScopedInitialized = true;
    }
    if (!ScopedInitialized)
    {
        LogWarn("tts",
                "native TTS (speech-dispatcher) is unavailable; BlindMode keeps the narrative log but cannot "
                "speak it");
    }
    return ScopedInitialized && ScopedBackend.speak != nullptr ? &ScopedBackend : nullptr;
#else
    return nullptr;
#endif
}

std::uint32_t EnumerateNativeTtsVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    const TtsBackend* ScopedBackend = NativeTtsBackend();
    if (ScopedBackend == nullptr || ScopedBackend->enumerateVoices == nullptr)
    {
        return 0;
    }
    return ScopedBackend->enumerateVoices(ScopedBackend->user, OutVoices, InCapacity);
}

void ShutdownNativeTts() noexcept
{
#if defined(_WIN32)
    CompositeInstance().Shutdown();
#elif defined(__APPLE__)
    AvSpeechInstance().Shutdown();
#elif defined(__linux__)
    SpeechDispatcherInstance().Shutdown();
#endif
}
}
