// FemboyEngine2D - narrator implementation.
#include "Engine/UI/Narrator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] std::uint32_t ClampVerbosity(std::uint32_t InVerbosity) noexcept
{
    return InVerbosity > 3 ? 3 : InVerbosity;
}

// Splits a long line on a word boundary so one utterance never exceeds the
// backend buffer. Returns the byte count of the first chunk.
[[nodiscard]] std::size_t ChunkLength(std::string_view InText, std::size_t InLimit) noexcept
{
    if (InText.size() <= InLimit)
    {
        return InText.size();
    }
    const std::size_t ScopedLimit = InLimit;
    std::size_t ScopedCut = InText.rfind(' ', ScopedLimit);
    if (ScopedCut == std::string_view::npos || ScopedCut < ScopedLimit / 2)
    {
        ScopedCut = ScopedLimit;
    }
    else
    {
        ++ScopedCut;
    }
    return ScopedCut;
}
}

void Narrator::SetSettings(const NarratorSettings& InSettings) noexcept
{
    const char ScopedPreviousLanguage[16] = {
        MSettings.Language[0],  MSettings.Language[1],  MSettings.Language[2],  MSettings.Language[3],
        MSettings.Language[4],  MSettings.Language[5],  MSettings.Language[6],  MSettings.Language[7],
        MSettings.Language[8],  MSettings.Language[9],  MSettings.Language[10], MSettings.Language[11],
        MSettings.Language[12], MSettings.Language[13], MSettings.Language[14], MSettings.Language[15]};

    char ScopedPreviousVoiceId[kTtsVoiceIdBytes] = {};
    std::memcpy(ScopedPreviousVoiceId, MSettings.VoiceId, sizeof(ScopedPreviousVoiceId) - 1);

    MSettings = InSettings;
    MSettings.Rate = std::clamp(MSettings.Rate, 0.5f, 2.0f);
    MSettings.MaxQueued = std::clamp(MSettings.MaxQueued, 1u, kMaxUtterances);
    MSettings.Verbosity = static_cast<std::uint8_t>(ClampVerbosity(MSettings.Verbosity));
    MSettings.Language[sizeof(MSettings.Language) - 1] = '\0';
    MSettings.VoiceId[sizeof(MSettings.VoiceId) - 1] = '\0';
    if (MSettings.MaxQueued < MCount)
    {
        // Shrinking the queue drops the oldest, lowest-priority lines first.
        while (MCount > MSettings.MaxQueued)
        {
            Utterance ScopedDropped;
            (void)PopNext(ScopedDropped);
            ++MDropped;
        }
    }
    if (MBackend != nullptr && MBackend->setRate != nullptr)
    {
        MBackend->setRate(MBackend->user, MSettings.Rate);
    }
    if (std::strcmp(ScopedPreviousLanguage, MSettings.Language) != 0 ||
        std::strcmp(ScopedPreviousVoiceId, MSettings.VoiceId) != 0)
    {
        MLanguageApplied = false;
        ApplyVoice();
    }
}

void Narrator::SetBackend(const TtsBackend* InBackend) noexcept
{
    MBackend = InBackend;
    MLanguageApplied = false;
    if (MBackend == nullptr)
    {
        return;
    }
    if (MBackend->setRate != nullptr)
    {
        MBackend->setRate(MBackend->user, MSettings.Rate);
    }
    ApplyVoice();
}

void Narrator::SetSink(Sink InSink, void* InUser) noexcept
{
    MSink = InSink;
    MSinkUser = InUser;
}

void Narrator::SetRoleNameResolver(RoleNameResolver InResolver, void* InUser) noexcept
{
    MRoleResolver = InResolver;
    MRoleUser = InUser;
}

bool Narrator::SetLanguage(std::string_view InBcp47) noexcept
{
    if (InBcp47.empty())
    {
        return false;
    }
    const std::size_t ScopedLength = std::min(InBcp47.size(), sizeof(MSettings.Language) - 1);
    std::memset(MSettings.Language, 0, sizeof(MSettings.Language));
    std::memcpy(MSettings.Language, InBcp47.data(), ScopedLength);
    // An explicit language request is the "auto voice" mode.
    MSettings.VoiceId[0] = '\0';
    MLanguageApplied = false;
    ApplyVoice();
    return MBackend != nullptr;
}

void Narrator::ApplyVoice() noexcept
{
    if (MBackend == nullptr || MLanguageApplied)
    {
        return;
    }
    // The locale change is a main-thread request: the backend selects the voice
    // there. An explicit voice id wins while the backend still has that voice.
    if (MSettings.VoiceId[0] != '\0' && MBackend->setVoiceById != nullptr &&
        MBackend->setVoiceById(MBackend->user, MSettings.VoiceId))
    {
        MLanguageApplied = true;
        return;
    }
    if (MBackend->setVoice == nullptr)
    {
        return;
    }
    MBackend->setVoice(MBackend->user, MSettings.Language);
    MLanguageApplied = true;
}

std::string_view Narrator::RoleName(Ui::AccessibilityRole InRole) const noexcept
{
    if (MRoleResolver != nullptr)
    {
        const std::string_view ScopedName = MRoleResolver(MRoleUser, InRole);
        if (!ScopedName.empty())
        {
            return ScopedName;
        }
    }
    return std::string_view(Ui::AccessibilityRoleName(InRole));
}

std::uint32_t Narrator::ComposeFocus(const Ui::AccessibilityTree& InTree, std::uint32_t InIndex, char* OutText,
                                     std::size_t InCapacity) noexcept
{
    if (OutText == nullptr || InCapacity == 0)
    {
        return 0;
    }
    const Ui::AccessibilityNodeView ScopedNode = InTree.At(InIndex);
    char ScopedName[128]{};
    InTree.NameOf(ScopedNode, ScopedName, sizeof(ScopedName));

    char ScopedRoleBuffer[48]{};
    const char* ScopedRole = "";
    if (MSettings.Verbosity >= 2 && ScopedNode.Role != Ui::AccessibilityRole::Unknown)
    {
        const std::string_view ScopedRoleName = RoleName(ScopedNode.Role);
        // The resolver returns a view; a view is not required to be NUL
        // terminated, so the role is copied into the local buffer.
        std::snprintf(ScopedRoleBuffer, sizeof(ScopedRoleBuffer), "%.*s",
                      static_cast<int>(std::min<std::size_t>(ScopedRoleName.size(), sizeof(ScopedRoleBuffer) - 1)),
                      ScopedRoleName.data());
        ScopedRole = ScopedRoleBuffer;
    }

    char ScopedState[96]{};
    if (MSettings.Verbosity >= 3 && MSettings.AnnounceState)
    {
        std::size_t ScopedUsed = 0;
        const auto ScopedAppend = [&](const char* InWord)
        {
            if (InWord == nullptr || InWord[0] == '\0')
            {
                return;
            }
            const std::size_t ScopedLength = std::strlen(InWord);
            if (ScopedUsed + ScopedLength + 3 >= sizeof(ScopedState))
            {
                return;
            }
            if (ScopedUsed > 0)
            {
                ScopedState[ScopedUsed++] = ',';
                ScopedState[ScopedUsed++] = ' ';
            }
            std::memcpy(ScopedState + ScopedUsed, InWord, ScopedLength);
            ScopedUsed += ScopedLength;
            ScopedState[ScopedUsed] = '\0';
        };

        const bool ScopedChecked = (ScopedNode.States & Ui::AccessibilityStateChecked) != 0;
        if (ScopedNode.Role == Ui::AccessibilityRole::CheckBox ||
            ScopedNode.Role == Ui::AccessibilityRole::RadioButton ||
            ScopedNode.Role == Ui::AccessibilityRole::ToggleButton)
        {
            ScopedAppend(ScopedChecked ? "checked" : "not checked");
        }
        if ((ScopedNode.States & Ui::AccessibilityStateReadOnly) != 0)
        {
            ScopedAppend("read only");
        }
        if ((ScopedNode.States & Ui::AccessibilityStateEnabled) == 0)
        {
            ScopedAppend("disabled");
        }

        if (ScopedState[0] != '\0')
        {
            // The comma reads as a pause between the role and the state block.
            std::snprintf(OutText, InCapacity, "%s%s%s, %s", ScopedName, ScopedRole[0] != '\0' ? ", " : "", ScopedRole,
                          ScopedState);
            return static_cast<std::uint32_t>(std::strlen(OutText));
        }
    }

    if (MSettings.Verbosity <= 1)
    {
        std::snprintf(OutText, InCapacity, "%s", ScopedName);
    }
    else if (ScopedRole[0] != '\0')
    {
        std::snprintf(OutText, InCapacity, "%s, %s", ScopedName, ScopedRole);
    }
    else
    {
        std::snprintf(OutText, InCapacity, "%s", ScopedName);
    }
    return static_cast<std::uint32_t>(std::strlen(OutText));
}

bool Narrator::AnnounceFocus(const Ui::AccessibilityTree& InTree, std::uint32_t InIndex) noexcept
{
    if (!MSettings.Enabled || !MSettings.AnnounceFocus || InIndex >= InTree.Count())
    {
        return false;
    }
    char ScopedText[256]{};
    if (ComposeFocus(InTree, InIndex, ScopedText, sizeof(ScopedText)) == 0)
    {
        return false;
    }
    const Ui::AccessibilityNodeView ScopedNode = InTree.At(InIndex);
    // A focus move is an interrupt, not a queued line: the user has already
    // moved on, and reading the previous widget would be wrong.
    const NarratorPriority ScopedPriority =
        MSettings.InterruptOnFocus ? NarratorPriority::Critical : NarratorPriority::High;
    return Enqueue(MOwner, ScopedPriority, ScopedText, ScopedNode.Id);
}

bool Narrator::Enqueue(NarratorOwner InOwner, NarratorPriority InPriority, std::string_view InText,
                       std::uint32_t InDedupKey) noexcept
{
    if (!IsOwnedBy(InOwner) || !MSettings.Enabled || InText.empty())
    {
        ++MDropped;
        return false;
    }
    if (InDedupKey != 0 && MDedupKey == InDedupKey && MClockMs - MDedupAtMs < kDedupWindowMs)
    {
        ++MDropped;
        return false;
    }
    MDedupKey = InDedupKey;
    MDedupAtMs = MClockMs;

    if (InPriority == NarratorPriority::Critical && MSettings.InterruptOnFocus && MSpeaking && MBackend != nullptr &&
        MBackend->stop != nullptr)
    {
        MBackend->stop(MBackend->user);
        MSpeaking = false;
        MSpeakRemainingMs = 0.0f;
    }

    bool ScopedQueued = false;
    std::string_view ScopedRemaining = InText;
    while (!ScopedRemaining.empty())
    {
        const std::size_t ScopedLength = ChunkLength(ScopedRemaining, kMaxUtteranceBytes - 1);
        Utterance ScopedUtterance;
        ScopedUtterance.Priority = InPriority;
        ScopedUtterance.DedupKey = InDedupKey;
        ScopedUtterance.Length = static_cast<std::uint16_t>(ScopedLength);
        std::memcpy(ScopedUtterance.Text, ScopedRemaining.data(), ScopedLength);
        ScopedUtterance.Text[ScopedLength] = '\0';
        if (!Push(ScopedUtterance))
        {
            break;
        }
        ScopedQueued = true;
        ScopedRemaining.remove_prefix(ScopedLength);
        // A long line is split, but the queue budget still applies.
        if (MCount >= MSettings.MaxQueued)
        {
            break;
        }
    }
    return ScopedQueued;
}

bool Narrator::Push(const Utterance& InUtterance) noexcept
{
    if (MCount >= MSettings.MaxQueued)
    {
        // Replace the lowest-priority, oldest line only for a strictly more
        // important one; otherwise the new line is the one that is dropped.
        std::uint32_t ScopedWorst = 0;
        for (std::uint32_t ScopedIndex = 1; ScopedIndex < MCount; ++ScopedIndex)
        {
            if (MQueue[ScopedIndex].Priority < MQueue[ScopedWorst].Priority)
            {
                ScopedWorst = ScopedIndex;
            }
        }
        if (InUtterance.Priority <= MQueue[ScopedWorst].Priority)
        {
            ++MDropped;
            return false;
        }
        for (std::uint32_t ScopedIndex = ScopedWorst; ScopedIndex + 1 < MCount; ++ScopedIndex)
        {
            MQueue[ScopedIndex] = MQueue[ScopedIndex + 1];
        }
        --MCount;
    }

    // Insertion keeps the array sorted by priority, FIFO inside a priority.
    std::uint32_t ScopedSlot = MCount;
    while (ScopedSlot > 0 && MQueue[ScopedSlot - 1].Priority < InUtterance.Priority)
    {
        MQueue[ScopedSlot] = MQueue[ScopedSlot - 1];
        --ScopedSlot;
    }
    MQueue[ScopedSlot] = InUtterance;
    ++MCount;
    return true;
}

bool Narrator::PopNext(Utterance& OutUtterance) noexcept
{
    if (MCount == 0)
    {
        return false;
    }
    OutUtterance = MQueue[0];
    for (std::uint32_t ScopedIndex = 0; ScopedIndex + 1 < MCount; ++ScopedIndex)
    {
        MQueue[ScopedIndex] = MQueue[ScopedIndex + 1];
    }
    --MCount;
    return true;
}

void Narrator::SpeakNow(const Utterance& InUtterance) noexcept
{
    const std::string_view ScopedText(InUtterance.Text, InUtterance.Length);
    const TtsPriority ScopedPriority =
        InUtterance.Priority >= NarratorPriority::High ? TtsPriority::Interrupt : TtsPriority::Normal;
    if (MBackend != nullptr && MBackend->speak != nullptr)
    {
        (void)MBackend->speak(MBackend->user, ScopedText.data(), ScopedText.size(), ScopedPriority);
    }
    else if (MSink != nullptr)
    {
        MSink(MSinkUser, ScopedText.data(), ScopedText.size());
    }
    else
    {
        // No output path at all: keep the queue moving so a later backend
        // installation does not replay stale lines.
        MSpeaking = false;
        return;
    }
    ++MSpoken;
    MSpeaking = true;
    const float ScopedRate = MSettings.Rate > 0.01f ? MSettings.Rate : 1.0f;
    MSpeakRemainingMs = static_cast<float>(ScopedText.size()) / (kCharactersPerSecond * ScopedRate) * 1000.0f + 120.0f;
}

void Narrator::Advance(float InDeltaSeconds) noexcept
{
    float ScopedDelta = InDeltaSeconds;
    if (ScopedDelta <= 0.0f)
    {
        const auto ScopedNow = std::chrono::steady_clock::now();
        if (MClockStarted)
        {
            ScopedDelta = std::chrono::duration<float>(ScopedNow - MLastTick).count();
        }
        MLastTick = ScopedNow;
        MClockStarted = true;
    }
    if (ScopedDelta > 0.0f)
    {
        MClockMs += static_cast<std::uint64_t>(ScopedDelta * 1000.0f);
        if (MSpeaking)
        {
            MSpeakRemainingMs -= ScopedDelta * 1000.0f;
            if (MSpeakRemainingMs <= 0.0f)
            {
                MSpeaking = false;
                MSpeakRemainingMs = 0.0f;
            }
        }
    }
    if (!MSpeaking && MCount > 0 && MSettings.Enabled)
    {
        // A missing output path would otherwise hold one line per frame; the
        // bounded loop drains the queue without a per-frame stall.
        for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < kMaxUtterances && !MSpeaking && MCount > 0;
             ++ScopedAttempt)
        {
            Utterance ScopedNext;
            if (!PopNext(ScopedNext))
            {
                break;
            }
            SpeakNow(ScopedNext);
        }
    }
}

void Narrator::Stop() noexcept
{
    Clear();
    if (MBackend != nullptr && MBackend->stop != nullptr)
    {
        MBackend->stop(MBackend->user);
    }
    MSpeaking = false;
    MSpeakRemainingMs = 0.0f;
}

void Narrator::Clear() noexcept
{
    MCount = 0;
    MDedupKey = 0;
    MDedupAtMs = 0;
}
}
