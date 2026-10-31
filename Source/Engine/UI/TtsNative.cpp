// FemboyEngine2D - platform TTS selection.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

namespace Fby
{
namespace
{
#if defined(_WIN32)
SapiTts& SapiInstance() noexcept
{
    static SapiTts ScopedInstance;
    return ScopedInstance;
}
#elif defined(__APPLE__)
AvSpeechTts& AvSpeechInstance() noexcept
{
    static AvSpeechTts ScopedInstance;
    return ScopedInstance;
}
#endif
}

const TtsBackend* NativeTtsBackend() noexcept
{
    // The first successful call constructs the OS synthesizer (COM apartment or
    if (!RequireMainThread("NativeTtsBackend"))
    {
        return nullptr;
    }
#if defined(_WIN32)
    static TtsBackend ScopedBackend;
    static bool ScopedInitialized = false;
    if (!ScopedInitialized && SapiInstance().Initialize())
    {
        ScopedBackend = SapiInstance().Backend();
        ScopedInitialized = true;
    }
    if (!ScopedInitialized)
    {
        LogWarn("tts", "native TTS (SAPI) is unavailable; BlindMode keeps the narrative log but cannot speak it");
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
#else
    return nullptr;
#endif
}

void ShutdownNativeTts() noexcept
{
#if defined(_WIN32)
    SapiInstance().Shutdown();
#elif defined(__APPLE__)
    AvSpeechInstance().Shutdown();
#endif
}
}
