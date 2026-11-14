// FemboyEngine2D - speech-dispatcher adapter contract test.
//
// The Linux CI runner may or may not have speech-dispatcher installed. Both
// states are valid: without the daemon the adapter must fail closed and the
// narrator keeps its log-only path, with it the calls must not crash and the
// language/rate mapping must stay bounded.
#include "Engine/UI/Tts.hpp"

#include <cstdio>

#if defined(__linux__)
int main()
{
    Fby::SpeechDispatcherTts ScopedTts;
    const bool ScopedReady = ScopedTts.Initialize();
    std::printf("tts_speech_dispatcher: ready=%d\n", ScopedReady ? 1 : 0);
    if (!ScopedReady)
    {
        // No client library or no daemon: the engine must expose no backend.
        if (ScopedTts.IsReady())
        {
            std::fprintf(stderr, "FAIL: not ready but IsReady() is true\n");
            return 1;
        }
        std::printf("tts_speech_dispatcher: fallback path OK\n");
        return 0;
    }

    ScopedTts.SetRate(1.0f);
    ScopedTts.SetRate(-10.0f);
    ScopedTts.SetRate(10.0f);
    ScopedTts.SetRate(1000.0f);
    ScopedTts.SetVoice("en-US");
    ScopedTts.SetVoice(nullptr);
    (void)ScopedTts.Speak("FemboyEngine2D speech-dispatcher test", Fby::TtsPriority::Normal);
    (void)ScopedTts.Speak("interrupt", Fby::TtsPriority::Interrupt);
    ScopedTts.Stop();

    Fby::TtsVoiceInfo ScopedVoices[4]{};
    if (ScopedTts.EnumerateVoices(ScopedVoices, 4) != 0)
    {
        std::fprintf(stderr, "FAIL: speech-dispatcher has no enumerable voice ids\n");
        return 1;
    }
    if (ScopedTts.SetVoiceById("voice"))
    {
        std::fprintf(stderr, "FAIL: SetVoiceById must report unsupported\n");
        return 1;
    }
    ScopedTts.Shutdown();
    if (ScopedTts.IsReady())
    {
        std::fprintf(stderr, "FAIL: IsReady() after Shutdown()\n");
        return 1;
    }
    std::printf("tts_speech_dispatcher: all checks passed\n");
    return 0;
}
#else
int main()
{
    std::printf("tts_speech_dispatcher: skipped on this platform\n");
    return 0;
}
#endif
