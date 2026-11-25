// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// FemboyEngine2D - data-driven action narration implementation.
#include "Engine/UI/ActionNarration.hpp"

#include "Engine/Assets/Localization.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kMaxComposedBytes = 512;

void AppendText(char* OutText, std::size_t InCapacity, std::size_t& UsedBytes, std::string_view InText) noexcept
{
    if (InText.empty() || UsedBytes + 1 >= InCapacity)
    {
        return;
    }
    const std::size_t ScopedRoom = InCapacity - UsedBytes - 1;
    const std::size_t ScopedLength = std::min(InText.size(), ScopedRoom);
    std::memcpy(OutText + UsedBytes, InText.data(), ScopedLength);
    UsedBytes += ScopedLength;
    OutText[UsedBytes] = '\0';
}

bool TokenEquals(std::string_view InToken, const char* InName) noexcept
{
    return InToken == std::string_view(InName);
}

// Removes the artefacts an empty placeholder can leave behind: doubled spaces
// and a space before punctuation. The speech stays grammatical when a game
// omits the direction word or the object name.
std::size_t CleanSpokenText(char* SpokenText, std::size_t InLength) noexcept
{
    std::size_t ScopedRead = 0;
    std::size_t ScopedWrite = 0;
    bool ScopedPendingSpace = false;
    while (ScopedRead < InLength)
    {
        const char ScopedChar = SpokenText[ScopedRead++];
        if (ScopedChar == ' ')
        {
            if (ScopedWrite == 0)
            {
                continue;
            }
            ScopedPendingSpace = true;
            continue;
        }
        const bool ScopedPunctuation = ScopedChar == ',' || ScopedChar == '.' || ScopedChar == ';' ||
                                       ScopedChar == ':' || ScopedChar == '!' || ScopedChar == '?';
        if (ScopedPendingSpace && ScopedWrite > 0 && !ScopedPunctuation)
        {
            SpokenText[ScopedWrite++] = ' ';
        }
        ScopedPendingSpace = false;
        SpokenText[ScopedWrite++] = ScopedChar;
    }
    SpokenText[ScopedWrite] = '\0';
    return ScopedWrite;
}
}

void ActionNarration::SetLocalization(const LocalizationTable* InTable) noexcept
{
    MTable = InTable;
}

void ActionNarration::SetVocabulary(const ActionVocabulary& InVocabulary) noexcept
{
    MVocabulary = InVocabulary;
}

void ActionNarration::SetSettings(const ActionNarrationSettings& InSettings) noexcept
{
    MSettings = InSettings;
    // Keep the thresholds ordered so a misconfigured project cannot turn every
    // distance into "far".
    if (MSettings.FarDistanceMeters < MSettings.NearDistanceMeters)
    {
        MSettings.FarDistanceMeters = MSettings.NearDistanceMeters;
    }
    MSettings.NearDistanceMeters = std::max(0.0f, MSettings.NearDistanceMeters);
}

void ActionNarration::Advance(float InDeltaSeconds) noexcept
{
    if (InDeltaSeconds <= 0.0f)
    {
        return;
    }
    MDedupClockMs += static_cast<std::uint64_t>(InDeltaSeconds * 1000.0f);
}

void ActionNarration::Clear() noexcept
{
    MDedupCount = 0;
    std::memset(MDedup, 0, sizeof(MDedup));
}

std::uint32_t ActionNarration::Resolve(std::uint32_t InId, std::string_view InFallback, char* OutText,
                                       std::size_t InCapacity) const noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::string_view ScopedText;
    if (InId != 0 && MTable != nullptr)
    {
        ScopedText = MTable->FindWithFallback(InId);
    }
    if (ScopedText.empty())
    {
        ScopedText = InFallback;
    }
    const std::size_t ScopedLength = std::min(ScopedText.size(), InCapacity - 1);
    std::memcpy(OutText, ScopedText.data(), ScopedLength);
    OutText[ScopedLength] = '\0';
    return static_cast<std::uint32_t>(ScopedLength);
}

std::uint8_t ActionNarration::DirectionBucket(float InRadians) const noexcept
{
    // Eight sectors of 45 degrees. Normalize into [0, 2pi) without a loop over
    // unbounded input.
    if (!std::isfinite(InRadians))
    {
        return 0;
    }
    float ScopedAngle = std::fmod(InRadians, 2.0f * kPi);
    if (ScopedAngle < 0.0f)
    {
        ScopedAngle += 2.0f * kPi;
    }
    const float ScopedSector = ScopedAngle * 8.0f / (2.0f * kPi);
    const auto ScopedIndex = static_cast<std::uint32_t>(std::floor(ScopedSector + 0.5f)) & 7u;
    return static_cast<std::uint8_t>(ScopedIndex);
}

std::uint32_t ActionNarration::DistanceWord(float InMeters) const noexcept
{
    if (InMeters < 0.0f)
    {
        return 0;
    }
    if (InMeters <= MSettings.NearDistanceMeters)
    {
        return MVocabulary.Near;
    }
    if (InMeters <= MSettings.FarDistanceMeters)
    {
        return MVocabulary.Medium;
    }
    return MVocabulary.Far;
}

std::uint32_t ActionNarration::Compose(const ActionEvent& InEvent, char* OutText, std::size_t InCapacity) const noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return 0;
    }
    OutText[0] = '\0';
    if (!MSettings.Enabled || InEvent.TemplateId == 0)
    {
        return 0;
    }

    std::string_view ScopedTemplate;
    if (MTable != nullptr)
    {
        ScopedTemplate = InEvent.Count > 0
                             ? MTable->FindVariant(InEvent.TemplateId, InEvent.Count, LocalizationGender::Neutral)
                             : MTable->FindWithFallback(InEvent.TemplateId);
    }
    if (ScopedTemplate.empty())
    {
        return 0;
    }

    char ScopedSubject[96]{};
    char ScopedObject[96]{};
    (void)Resolve(InEvent.SubjectNameId, {}, ScopedSubject, sizeof(ScopedSubject));
    (void)Resolve(InEvent.ObjectNameId, {}, ScopedObject, sizeof(ScopedObject));

    char ScopedCount[32]{};
    if (InEvent.Count > 0)
    {
        std::snprintf(ScopedCount, sizeof(ScopedCount), "%lld", static_cast<long long>(InEvent.Count));
    }

    char ScopedDirection[48]{};
    if (MSettings.AnnounceDirection && InEvent.HasDirection)
    {
        const std::uint8_t ScopedBucket = DirectionBucket(InEvent.AngleRadians);
        (void)Resolve(MVocabulary.Directions[ScopedBucket], {}, ScopedDirection, sizeof(ScopedDirection));
    }

    char ScopedDistance[48]{};
    if (MSettings.AnnounceDistance && std::isfinite(InEvent.DistanceMeters) && InEvent.DistanceMeters >= 0.0f)
    {
        (void)Resolve(DistanceWord(InEvent.DistanceMeters), {}, ScopedDistance, sizeof(ScopedDistance));
    }

    char ScopedMeters[32]{};
    if (std::isfinite(InEvent.DistanceMeters) && InEvent.DistanceMeters >= 0.0f)
    {
        std::snprintf(ScopedMeters, sizeof(ScopedMeters), "%lld",
                      static_cast<long long>(std::lround(InEvent.DistanceMeters)));
    }

    std::size_t ScopedUsed = 0;
    std::size_t ScopedIndex = 0;
    while (ScopedIndex < ScopedTemplate.size())
    {
        const char ScopedChar = ScopedTemplate[ScopedIndex];
        if (ScopedChar != '{')
        {
            const std::size_t ScopedRun = ScopedTemplate.find('{', ScopedIndex);
            const std::size_t ScopedEnd = ScopedRun == std::string_view::npos ? ScopedTemplate.size() : ScopedRun;
            AppendText(OutText, InCapacity, ScopedUsed, ScopedTemplate.substr(ScopedIndex, ScopedEnd - ScopedIndex));
            ScopedIndex = ScopedEnd;
            continue;
        }
        const std::size_t ScopedClose = ScopedTemplate.find('}', ScopedIndex + 1);
        if (ScopedClose == std::string_view::npos)
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedTemplate.substr(ScopedIndex));
            break;
        }
        const std::string_view ScopedToken = ScopedTemplate.substr(ScopedIndex + 1, ScopedClose - ScopedIndex - 1);
        if (TokenEquals(ScopedToken, "subject"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedSubject);
        }
        else if (TokenEquals(ScopedToken, "object"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedObject);
        }
        else if (TokenEquals(ScopedToken, "count"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedCount);
        }
        else if (TokenEquals(ScopedToken, "direction"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedDirection);
        }
        else if (TokenEquals(ScopedToken, "distance"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedDistance);
        }
        else if (TokenEquals(ScopedToken, "meters"))
        {
            AppendText(OutText, InCapacity, ScopedUsed, ScopedMeters);
        }
        else
        {
            // Unknown tokens stay visible so an author sees the typo in the log.
            AppendText(OutText, InCapacity, ScopedUsed,
                       ScopedTemplate.substr(ScopedIndex, ScopedClose - ScopedIndex + 1));
        }
        ScopedIndex = ScopedClose + 1;
    }
    if (ScopedUsed == 0)
    {
        return 0;
    }
    const std::size_t ScopedCleaned = CleanSpokenText(OutText, ScopedUsed);
    return static_cast<std::uint32_t>(ScopedCleaned);
}

bool ActionNarration::Deduplicated(std::uint32_t InKey) const noexcept
{
    if (InKey == 0)
    {
        return false;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MDedupCount; ++ScopedIndex)
    {
        if (MDedup[ScopedIndex].Key != InKey)
        {
            continue;
        }
        const std::uint64_t ScopedAge = MDedupClockMs - MDedup[ScopedIndex].AtMs;
        return ScopedAge < MSettings.DedupWindowMs;
    }
    return false;
}

void ActionNarration::Remember(std::uint32_t InKey) noexcept
{
    if (InKey == 0)
    {
        return;
    }
    DedupEntry* ScopedSlot = nullptr;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MDedupCount; ++ScopedIndex)
    {
        if (MDedup[ScopedIndex].Key == InKey)
        {
            ScopedSlot = &MDedup[ScopedIndex];
            break;
        }
    }
    if (ScopedSlot == nullptr && MDedupCount < kMaxDedupEntries)
    {
        ScopedSlot = &MDedup[MDedupCount++];
    }
    if (ScopedSlot == nullptr)
    {
        // Replace the oldest entry; the array is small and insertion is cheap.
        ScopedSlot = &MDedup[0];
        for (std::uint32_t ScopedIndex = 1; ScopedIndex < kMaxDedupEntries; ++ScopedIndex)
        {
            if (MDedup[ScopedIndex].AtMs < ScopedSlot->AtMs)
            {
                ScopedSlot = &MDedup[ScopedIndex];
            }
        }
    }
    ScopedSlot->Key = InKey;
    ScopedSlot->AtMs = MDedupClockMs;
}

bool ActionNarration::Publish(const ActionEvent& InEvent, Narrator& InNarrator) noexcept
{
    if (!MSettings.Enabled || InEvent.Category >= kNarrationCategoryCount)
    {
        return false;
    }
    if ((MSettings.Categories & (1u << InEvent.Category)) == 0)
    {
        return false;
    }
    if (InEvent.Priority < MSettings.MinimumPriority)
    {
        return false;
    }
    if (Deduplicated(InEvent.DedupKey))
    {
        return false;
    }

    char ScopedLine[kMaxComposedBytes]{};
    if (Compose(InEvent, ScopedLine, sizeof(ScopedLine)) == 0)
    {
        return false;
    }
    const auto ScopedPriority = static_cast<NarratorPriority>(std::min<std::uint8_t>(InEvent.Priority, 3));
    if (!InNarrator.Enqueue(InNarrator.Owner(), ScopedPriority, ScopedLine, InEvent.DedupKey))
    {
        return false;
    }
    Remember(InEvent.DedupKey);
    return true;
}
}
