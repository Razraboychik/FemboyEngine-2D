// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// FemboyEngine2D - data-driven action narration.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Entity.hpp"
#include "Engine/UI/Narrator.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fby
{
class LocalizationTable;

// What kind of action a line describes. Every category can be muted on its own
// so a player can keep combat callouts and silence pickups, or the other way
// around.
enum class NarrationCategory : std::uint8_t
{
    Interaction = 0,
    Combat = 1,
    Item = 2,
    Navigation = 3,
    State = 4,
    System = 5,
    Count = 6,
};

inline constexpr std::uint32_t kNarrationCategoryCount = static_cast<std::uint32_t>(NarrationCategory::Count);
using NarrationCategoryMask = std::uint32_t;
inline constexpr NarrationCategoryMask kAllNarrationCategories = (1u << kNarrationCategoryCount) - 1u;

// Localized words the templates may reference. A zero id means "the game does
// not provide this word"; the placeholder is then replaced with nothing instead
// of leaking a raw id into speech.
struct ActionVocabulary
{
    [[nodiscard]] friend bool operator==(const ActionVocabulary&, const ActionVocabulary&) noexcept = default;

    // Clockwise from ahead: ahead, ahead right, right, behind right, behind,
    // behind left, left, ahead left.
    std::uint32_t Directions[8]{};
    std::uint32_t Near = 0;
    std::uint32_t Medium = 0;
    std::uint32_t Far = 0;
};

// Policy of the action layer. Lives in AccessibilityProfile::actions and is
// stored next to the rest of the accessibility profile.
struct ActionNarrationSettings
{
    [[nodiscard]] friend bool operator==(const ActionNarrationSettings&,
                                         const ActionNarrationSettings&) noexcept = default;

    bool Enabled = true;
    bool AnnounceDirection = true;
    // Distance is spoken as a localized bucket word (near/medium/far) because it
    // stays understandable when the exact metre value is noise.
    bool AnnounceDistance = true;
    NarrationCategoryMask Categories = kAllNarrationCategories;
    // 0 speaks every priority, 3 speaks critical lines only.
    std::uint8_t MinimumPriority = 0;
    float NearDistanceMeters = 3.0f;
    float FarDistanceMeters = 12.0f;
    // A repeated DedupKey inside this window is dropped before composing.
    std::uint16_t DedupWindowMs = 1200;
};

// One thing that happened. Systems publish this instead of pre-formatted text;
// the layer owns wording, plural rules, direction and distance grammar, and the
// narrator owns the queue.
struct ActionEvent
{
    // Localization id of the template. Supports plural variants through
    // LocalizationTable::FindVariant(TemplateId, Count).
    std::uint32_t TemplateId = 0;
    // Optional localized names injected as {subject} and {object}.
    std::uint32_t SubjectNameId = 0;
    std::uint32_t ObjectNameId = 0;
    // 0 disables deduplication; otherwise repeated keys inside the window drop.
    std::uint32_t DedupKey = 0;
    Entity Subject = Fby::kNullEntity;
    Entity Object = Fby::kNullEntity;
    Vector2D Position{};
    // 0 means "not counted"; plural variants are asked when it is positive.
    std::int64_t Count = 0;
    // Negative means "unknown"; the distance placeholder is then empty.
    float DistanceMeters = -1.0f;
    // Radians, 0 = ahead and positive = clockwise (screen convention).
    float AngleRadians = 0.0f;
    bool HasDirection = false;
    std::uint8_t Category = 0;
    // 0 low, 1 normal, 2 high, 3 critical.
    std::uint8_t Priority = 1;
};

class ActionNarration
{
public:
    static constexpr std::uint32_t kMaxDedupEntries = 16;

    ActionNarration() = default;

    // The table is owned by the game and must outlive the layer. Replacing it
    // keeps the deduplication state.
    void SetLocalization(const LocalizationTable* InTable) noexcept;
    void SetVocabulary(const ActionVocabulary& InVocabulary) noexcept;
    void SetSettings(const ActionNarrationSettings& InSettings) noexcept;
    [[nodiscard]] const ActionNarrationSettings& Settings() const noexcept
    {
        return MSettings;
    }

    // Frames the deduplication clock. Call once per frame on the owner thread.
    void Advance(float InDeltaSeconds) noexcept;
    // Drops the deduplication history, keeping settings and vocabulary.
    void Clear() noexcept;

    // Formats the template and returns the bytes written, or 0 when the event
    // cannot be spoken (unknown template, no subject, disabled).
    [[nodiscard]] std::uint32_t Compose(const ActionEvent& InEvent, char* OutText,
                                        std::size_t InCapacity) const noexcept;

    // Applies the settings gates, composes and queues the line in the narrator.
    // Returns true when a line was queued.
    bool Publish(const ActionEvent& InEvent, Narrator& InNarrator) noexcept;

private:
    struct DedupEntry
    {
        std::uint32_t Key = 0;
        std::uint64_t AtMs = 0;
    };

    [[nodiscard]] bool Deduplicated(std::uint32_t InKey) const noexcept;
    void Remember(std::uint32_t InKey) noexcept;
    [[nodiscard]] std::uint32_t Resolve(std::uint32_t InId, std::string_view InFallback, char* OutText,
                                        std::size_t InCapacity) const noexcept;
    [[nodiscard]] std::uint8_t DirectionBucket(float InRadians) const noexcept;
    [[nodiscard]] std::uint32_t DistanceWord(float InMeters) const noexcept;

    const LocalizationTable* MTable = nullptr;
    ActionVocabulary MVocabulary{};
    ActionNarrationSettings MSettings{};
    DedupEntry MDedup[kMaxDedupEntries]{};
    std::uint32_t MDedupCount = 0;
    std::uint64_t MDedupClockMs = 0;
};
}
