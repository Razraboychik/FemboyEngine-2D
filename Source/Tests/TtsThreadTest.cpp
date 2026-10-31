// FemboyEngine2D - TTS main-thread contract and voice selection tests.
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/Tts.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace
{
std::atomic<std::uint32_t> GSpeakCount{0};
std::atomic<std::uint32_t> GStopCount{0};
char GLastLanguage[32]{};

bool TestSpeak(void*, const char*, std::size_t, Fby::TtsPriority) noexcept
{
    GSpeakCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void TestStop(void*) noexcept
{
    GStopCount.fetch_add(1, std::memory_order_relaxed);
}

void TestSetVoice(void*, const char* InLanguage) noexcept
{
    if (InLanguage == nullptr)
    {
        GLastLanguage[0] = '\0';
        return;
    }
    std::strncpy(GLastLanguage, InLanguage, sizeof(GLastLanguage) - 1);
}

std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)
}

int main()
{
    Fby::TtsBackend ScopedBackend;
    ScopedBackend.speak = &TestSpeak;
    ScopedBackend.stop = &TestStop;
    ScopedBackend.setVoice = &TestSetVoice;

    Fby::AccessibilityKernel ScopedKernel;
    ScopedKernel.SetTtsBackend(&ScopedBackend);
    ScopedKernel.SetBlindMode(true);
    ScopedKernel.SetNarrativeLocale("ru-RU");
    FBY_CHECK(std::strcmp(GLastLanguage, "ru-RU") == 0);

    ScopedKernel.PublishNarrative(1, Fby::Entity{}, Fby::Vector2D{}, "Boss appeared", 0);

    // A worker must not reach SAPI/AVSpeech. FlushSpeech() is main-thread-only.
    std::thread ScopedWorker([&ScopedKernel]() { ScopedKernel.FlushSpeech(); });
    ScopedWorker.join();
    FBY_CHECK(GSpeakCount.load(std::memory_order_relaxed) == 0);

    // The owner (this thread) drains the same event.
    ScopedKernel.FlushSpeech();
    FBY_CHECK(GSpeakCount.load(std::memory_order_relaxed) == 1);

    ScopedKernel.SetNarrativeLocale("en-US");
    FBY_CHECK(std::strcmp(GLastLanguage, "en-US") == 0);

    // A locale change from a worker must not reach the backend. It is recorded
    GLastLanguage[0] = '\0';
    std::thread ScopedLocaleWorker([&ScopedKernel]() { ScopedKernel.SetNarrativeLocale("ru-RU"); });
    ScopedLocaleWorker.join();
    FBY_CHECK(GLastLanguage[0] == '\0');
    ScopedKernel.FlushSpeech();
    FBY_CHECK(std::strcmp(GLastLanguage, "ru-RU") == 0);

    ScopedKernel.SetBlindMode(false);
    FBY_CHECK(GStopCount.load(std::memory_order_relaxed) >= 1);

    if (GFailures == 0)
    {
        std::printf("tts_thread_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "tts_thread_test: %u failure(s)\n", GFailures);
    return 1;
}
