// FemboyEngine2D - speech language catalog and voice matching.
#pragma once

#include "Engine/UI/Tts.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{

// One language the narrator documents and tests as a first-class target. The
// catalog is not a promise that a voice is installed: it is the list the
// fallback policy and the documentation matrix are built from.
struct SpeechLanguageInfo
{
    // Canonical BCP-47 tag without a region ("pt", "zh").
    const char* Tag;
    const char* EnglishName;
    // Endonym, stored as universal character names so the source stays ASCII.
    const char* NativeName;
    const char* Script;
    bool RightToLeft;
};

// The catalog. Every tag is unique, lower-case and matches SpeechPrimarySubtag.
[[nodiscard]] std::uint32_t SpeechLanguageCount() noexcept;
[[nodiscard]] const SpeechLanguageInfo* SpeechLanguageAt(std::uint32_t InIndex) noexcept;
// Case-insensitive lookup by tag or by any tag with the same primary subtag
// ("pt-BR" finds "pt").
[[nodiscard]] const SpeechLanguageInfo* FindSpeechLanguage(std::string_view InTag) noexcept;

// Canonicalizes a tag: lower-case language, title-case four-letter script,
// upper-case two-letter region, variants preserved, underscore accepted as a
// separator. Returns false for an empty or oversized tag.
[[nodiscard]] bool NormalizeSpeechLanguage(std::string_view InTag, char* OutTag, std::size_t InCapacity) noexcept;
// Lower-case primary subtag view; empty when the tag is empty.
[[nodiscard]] std::string_view SpeechPrimarySubtag(std::string_view InTag) noexcept;

// 3 exact canonical match, 2 same primary subtag, 0 unrelated.
[[nodiscard]] std::uint8_t SpeechLanguageScore(std::string_view InRequested, std::string_view InInstalled) noexcept;

inline constexpr std::uint32_t kMaxSpeechFallbackTags = 3;
// Fills the fallback chain without duplicates: the requested tag, its primary
// subtag, then English. Returns the number of tags written.
[[nodiscard]] std::uint32_t BuildSpeechLanguageFallback(std::string_view InRequested,
                                                        char OutChain[kMaxSpeechFallbackTags][16]) noexcept;

inline constexpr std::uint32_t kNoSpeechVoice = 0xFFFFFFFFu;

// Best installed voice for a language request. Priority: exact tag, same
// primary subtag, English, then the first installed voice. When the request is
// empty the first voice wins. Returns kNoSpeechVoice when no voices exist.
[[nodiscard]] std::uint32_t FindBestSpeechVoice(const TtsVoiceInfo* InVoices, std::uint32_t InCount,
                                                std::string_view InRequested) noexcept;
}
