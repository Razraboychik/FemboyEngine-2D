// FemboyEngine2D - native text-to-speech bridge.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{

enum class TtsPriority : std::uint32_t
{
    // Urgent state change, cancel current speech
    Interrupt = 0,
    Normal = 1,
    Queued = 2,
};

struct TtsBackend
{
    void* user = nullptr;
    bool (*speak)(void* InUser, const char* InUtf8, std::size_t InLength, TtsPriority InPriority) noexcept = nullptr;
    void (*stop)(void* InUser) noexcept = nullptr;
    void (*setRate)(void* InUser, float InRate) noexcept = nullptr;
    // Select a voice by BCP-47 language tag ("en-US", "ru-RU"). Called from the
    void (*setVoice)(void* InUser, const char* InLanguageBCP47) noexcept = nullptr;
};

// Windows SAPI 5 adapter. The implementation is a separate TU; this header has
class SapiTts
{
public:
    SapiTts() = default;
    ~SapiTts();

    SapiTts(const SapiTts&) = delete;
    SapiTts& operator=(const SapiTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MVoice != nullptr;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    // Must be called from the OS main thread (SAPI is apartment-threaded).
    void SetVoice(const char* InLanguageBCP47) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    // ISpVoice*, hidden from this header
    void* MVoice = nullptr;
    bool MComInitialized = false;
    std::uint32_t MThreadId = 0;
};

// macOS AVSpeechSynthesizer adapter. The Objective-C++ implementation lives in
class AvSpeechTts
{
public:
    AvSpeechTts() = default;
    ~AvSpeechTts();

    AvSpeechTts(const AvSpeechTts&) = delete;
    AvSpeechTts& operator=(const AvSpeechTts&) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
        return MSynthesizer != nullptr;
    }
    [[nodiscard]] bool Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept;
    void Stop() noexcept;
    void SetRate(float InRate) noexcept;
    // Must be called from the main run loop. Resolves the BCP-47 tag against
    void SetVoice(const char* InLanguageBCP47) noexcept;
    [[nodiscard]] TtsBackend Backend() noexcept;

private:
    // AVSpeechSynthesizer*, retained through CFBridgingRetain
    void* MSynthesizer = nullptr;
    char MLanguage[16] = {};
    float MRate = 0.0f;
};

// Process-wide backend for the current platform (SAPI on Windows, AVSpeech on
[[nodiscard]] const TtsBackend* NativeTtsBackend() noexcept;
void ShutdownNativeTts() noexcept;
}
