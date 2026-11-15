// FemboyEngine2D - narrator: spoken accessibility output shared by the game and the editor.
#pragma once

#include "Engine/UI/AccessibilityTree.hpp"
#include "Engine/UI/Tts.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{

// Priority of a spoken line. Critical interrupts the current utterance when the
// settings allow it; Low is dropped first when the queue is full.
enum class NarratorPriority : std::uint8_t
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,
};

// Who owns the microphone of the process. The editor and a running Play In
// Editor session share one narrator, so exactly one of them speaks at a time.
enum class NarratorOwner : std::uint8_t
{
    None = 0,
    Editor = 1,
    Game = 2,
};

// Narrator policy. The game stores it in the project accessibility profile, the
// editor stores its own copy in the editor settings; both are plain PODs.
struct NarratorSettings
{
    [[nodiscard]] friend bool operator==(const NarratorSettings&, const NarratorSettings&) noexcept = default;

    bool Enabled = true;
    bool AnnounceFocus = true;
    bool AnnounceRole = true;
    bool AnnounceState = true;
    bool AnnounceScene = false;
    bool AnnounceWorldEvents = true;
    // A focus change cuts the current line instead of queueing behind it.
    bool InterruptOnFocus = true;
    // 0 silent, 1 name only, 2 name and role, 3 name, role and state.
    std::uint8_t Verbosity = 2;
    // Speech rate multiplier, clamped to 0.5..2.0 when applied.
    float Rate = 1.0f;
    // Maximum number of queued utterances; clamped to 1..16.
    std::uint32_t MaxQueued = 8;
    // BCP-47 tag of the spoken language, resolved against the installed voices.
    char Language[16] = "en-US";
    // Exact installed voice id (TtsVoiceInfo::Id). Empty means "auto": the voice
    // is resolved from Language. An explicit id wins while it is installed.
    char VoiceId[kTtsVoiceIdBytes] = {};
};

// Fixed-capacity speech queue. The backend is asynchronous (SAPI and AVSpeech
// both queue internally), so the narrator feeds one utterance at a time and
// estimates when the synthesizer is free again. That keeps a fast focus walk
// from stacking a hundred lines in the driver.
class Narrator
{
public:
    static constexpr std::uint32_t kMaxUtterances = 16;
    static constexpr std::uint32_t kMaxUtteranceBytes = 320;
    // Characters per second at rate 1.0. The estimate only gates the next
    // utterance; a backend that finishes earlier just waits for the next frame.
    static constexpr float kCharactersPerSecond = 14.0f;
    // A repeated announcement with the same key inside this window is dropped.
    static constexpr std::uint32_t kDedupWindowMs = 600;

    using Sink = void (*)(void* InUser, const char* InText, std::size_t InLength);
    // Optional localized role names. A game resolves them through its own
    // localization table; without one the English fallback is spoken.
    using RoleNameResolver = std::string_view (*)(void* InUser, Ui::AccessibilityRole InRole) noexcept;

    Narrator() = default;

    void SetSettings(const NarratorSettings& InSettings) noexcept;
    [[nodiscard]] const NarratorSettings& Settings() const noexcept
    {
        return MSettings;
    }

    void SetBackend(const TtsBackend* InBackend) noexcept;
    [[nodiscard]] const TtsBackend* Backend() const noexcept
    {
        return MBackend;
    }
    void SetSink(Sink InSink, void* InUser = nullptr) noexcept;
    void SetRoleNameResolver(RoleNameResolver InResolver, void* InUser = nullptr) noexcept;

    // Selects the spoken language. Returns false when the backend has no such
    // voice; the backend keeps its previous voice in that case.
    bool SetLanguage(std::string_view InBcp47) noexcept;
    [[nodiscard]] const char* Language() const noexcept
    {
        return MSettings.Language;
    }
    // Re-applies the configured voice to the backend: the explicit VoiceId when
    // it is set and installed, otherwise the Language search.
    void ApplyVoice() noexcept;

    void SetOwner(NarratorOwner InOwner) noexcept
    {
        MOwner = InOwner;
    }
    [[nodiscard]] NarratorOwner Owner() const noexcept
    {
        return MOwner;
    }
    [[nodiscard]] bool IsOwnedBy(NarratorOwner InOwner) const noexcept
    {
        return MOwner == NarratorOwner::None || MOwner == InOwner;
    }

    // Queues a line. `InDedupKey` suppresses an identical announcement inside
    // kDedupWindowMs; pass 0 to disable deduplication.
    bool Enqueue(NarratorOwner InOwner, NarratorPriority InPriority, std::string_view InText,
                 std::uint32_t InDedupKey = 0) noexcept;

    // Composes "name, role, state" from a tree node and queues it. Returns false
    // when nothing was queued (empty node, disabled focus narration, dedup).
    bool AnnounceFocus(const Ui::AccessibilityTree& InTree, std::uint32_t InIndex) noexcept;

    // Frames the clock. Call once per frame on the main thread.
    void Advance(float InDeltaSeconds) noexcept;
    // Drops the queue and stops the backend.
    void Stop() noexcept;
    // Drops the queue and resets the counters, keeping the settings.
    void Clear() noexcept;

    [[nodiscard]] std::uint32_t Queued() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::uint64_t Spoken() const noexcept
    {
        return MSpoken;
    }
    [[nodiscard]] std::uint64_t Dropped() const noexcept
    {
        return MDropped;
    }
    [[nodiscard]] bool Speaking() const noexcept
    {
        return MSpeaking;
    }
    // Milliseconds elapsed since the narrator was created; the character budget
    // is only advanced by Advance() so tests control time explicitly.
    [[nodiscard]] std::uint64_t ClockMs() const noexcept
    {
        return MClockMs;
    }

private:
    struct Utterance
    {
        NarratorPriority Priority = NarratorPriority::Normal;
        std::uint32_t DedupKey = 0;
        std::uint16_t Length = 0;
        char Text[kMaxUtteranceBytes]{};
    };

    [[nodiscard]] bool Push(const Utterance& InUtterance) noexcept;
    [[nodiscard]] bool PopNext(Utterance& OutUtterance) noexcept;
    void SpeakNow(const Utterance& InUtterance) noexcept;
    [[nodiscard]] std::string_view RoleName(Ui::AccessibilityRole InRole) const noexcept;
    [[nodiscard]] std::uint32_t ComposeFocus(const Ui::AccessibilityTree& InTree, std::uint32_t InIndex, char* OutText,
                                             std::size_t InCapacity) noexcept;

    NarratorSettings MSettings{};
    const TtsBackend* MBackend = nullptr;
    Sink MSink = nullptr;
    void* MSinkUser = nullptr;
    RoleNameResolver MRoleResolver = nullptr;
    void* MRoleUser = nullptr;
    NarratorOwner MOwner = NarratorOwner::None;

    std::array<Utterance, kMaxUtterances> MQueue{};
    std::uint32_t MCount = 0;

    std::uint64_t MClockMs = 0;
    // Wall clock fallback for a caller that only calls FlushSpeech(): a zero
    // delta is measured instead of freezing the speaking estimate forever.
    std::chrono::steady_clock::time_point MLastTick{};
    bool MClockStarted = false;
    std::uint64_t MSpoken = 0;
    std::uint64_t MDropped = 0;
    std::uint32_t MDedupKey = 0;
    std::uint64_t MDedupAtMs = 0;
    float MSpeakRemainingMs = 0.0f;
    bool MSpeaking = false;
    bool MLanguageApplied = false;
};
}
