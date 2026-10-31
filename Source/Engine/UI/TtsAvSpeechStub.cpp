// FemboyEngine2D - AVSpeechTts stub for non-Apple platforms.
#include "Engine/UI/Tts.hpp"

namespace Fby
{

AvSpeechTts::~AvSpeechTts() = default;

bool AvSpeechTts::Initialize() noexcept
{
    return false;
}

void AvSpeechTts::Shutdown() noexcept
{
}

bool AvSpeechTts::Speak(std::string_view InUtf8, TtsPriority InPriority) noexcept
{
    (void)InUtf8;
    (void)InPriority;
    return false;
}

void AvSpeechTts::Stop() noexcept
{
}

void AvSpeechTts::SetRate(float InRate) noexcept
{
    (void)InRate;
}

void AvSpeechTts::SetVoice(const char* InLanguageBCP47) noexcept
{
    (void)InLanguageBCP47;
}

TtsBackend AvSpeechTts::Backend() noexcept
{
    return TtsBackend{};
}
}
