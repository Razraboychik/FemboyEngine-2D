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

bool AvSpeechTts::SetVoiceById(const char* InId) noexcept
{
    (void)InId;
    return false;
}

std::uint32_t AvSpeechTts::EnumerateVoices(TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    (void)OutVoices;
    (void)InCapacity;
    return 0;
}

TtsBackend AvSpeechTts::Backend() noexcept
{
    return TtsBackend{};
}
}
