// FemboyEngine2D - platform TTS selection.
#include "Engine/UI/Tts.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/StoragePaths.hpp"
#include "Engine/Core/ThreadAffinity.hpp"

#include <cstdlib>
#include <string>

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

// The operating-system synthesizer, or null when the platform has none. A
// failure here is not fatal: voice packs can still provide speech.
const TtsBackend* OsBackend() noexcept
{
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
        LogWarn("tts", "native TTS (SAPI and one-core speech) is unavailable; voice packs may still speak");
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
        LogWarn("tts", "native TTS (AVSpeech) is unavailable; voice packs may still speak");
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
        LogWarn("tts", "native TTS (speech-dispatcher) is unavailable; voice packs may still speak");
    }
    return ScopedInitialized && ScopedBackend.speak != nullptr ? &ScopedBackend : nullptr;
#else
    return nullptr;
#endif
}

LayeredTts& LayeredInstance() noexcept
{
    static LayeredTts ScopedInstance;
    return ScopedInstance;
}

// Packs live next to the user's Saved data by default. FBY_VOICE_PACKS points
// the loader at a shipped or developer directory without a rebuild.
std::string VoicePackRoot()
{
    if (const char* ScopedEnvironment = std::getenv("FBY_VOICE_PACKS");
        ScopedEnvironment != nullptr && ScopedEnvironment[0] != '\0')
    {
        return std::string(ScopedEnvironment);
    }
    return (Core::StorageRoot() / "VoicePacks").string();
}
}

const TtsBackend* NativeTtsBackend() noexcept
{
    // The first successful call constructs the OS synthesizer and scans packs.
    if (!RequireMainThread("NativeTtsBackend"))
    {
        return nullptr;
    }
    static bool ScopedInitialized = false;
    if (!ScopedInitialized)
    {
        LayeredTts& ScopedLayered = LayeredInstance();
        ScopedInitialized = ScopedLayered.Initialize(OsBackend(), VoicePackRoot());
        if (!ScopedInitialized)
        {
            LogWarn("tts", "no TTS backend is available; BlindMode keeps the narrative log but cannot speak it");
        }
    }
    return ScopedInitialized ? LayeredInstance().Backend() : nullptr;
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
    LayeredInstance().Shutdown();
#if defined(_WIN32)
    CompositeInstance().Shutdown();
#elif defined(__APPLE__)
    AvSpeechInstance().Shutdown();
#elif defined(__linux__)
    SpeechDispatcherInstance().Shutdown();
#endif
}
}
