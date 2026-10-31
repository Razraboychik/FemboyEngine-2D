// FemboyEngine2D - accessibility kernel implementation.
#include "Engine/UI/Accessibility.hpp"

#include "Engine/Assets/Localization.hpp"
#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/ThreadAffinity.hpp"
#include "Engine/UI/Tts.hpp"
#include "Engine/Audio/Audio.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Fby
{
namespace
{

[[nodiscard]] float SrgbToLinear(float InValue) noexcept
{
    const float ScopedClamped = std::clamp(InValue, 0.0f, 1.0f);
    return ScopedClamped <= 0.04045f ? ScopedClamped / 12.92f : std::pow((ScopedClamped + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] float LinearToSrgb(float InValue) noexcept
{
    const float ScopedClamped = std::clamp(InValue, 0.0f, 1.0f);
    return ScopedClamped <= 0.0031308f ? ScopedClamped * 12.92f
                                       : 1.055f * std::pow(ScopedClamped, 1.0f / 2.4f) - 0.055f;
}
}

// Colour vision
ColorMatrix3 ColorVisionMatrix(ColorVisionDeficiency InDeficiency) noexcept
{
    // Machado et al. (2009) full-dichromacy matrices, linear-light sRGB. They
    switch (InDeficiency)
    {
        case ColorVisionDeficiency::Protanopia:
            return ColorMatrix3{
                {0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f}};
        case ColorVisionDeficiency::Deuteranopia:
            return ColorMatrix3{
                {0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f}};
        case ColorVisionDeficiency::Tritanopia:
            return ColorMatrix3{
                {1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f}};
        case ColorVisionDeficiency::None:
            break;
    }
    return ColorMatrix3{};
}

Color32 ApplyColorVision(Color32 InColor, const ColorMatrix3& InMatrix, float InStrength) noexcept
{
    const float ScopedBlend = std::clamp(InStrength, 0.0f, 1.0f);
    if (ScopedBlend <= 0.0f)
    {
        return InColor;
    }
    const float ScopedR = SrgbToLinear(static_cast<float>(InColor.R()) / 255.0f);
    const float ScopedG = SrgbToLinear(static_cast<float>(InColor.G()) / 255.0f);
    const float ScopedB = SrgbToLinear(static_cast<float>(InColor.B()) / 255.0f);

    const float ScopedCorrectedR = InMatrix.m[0] * ScopedR + InMatrix.m[1] * ScopedG + InMatrix.m[2] * ScopedB;
    const float ScopedCorrectedG = InMatrix.m[3] * ScopedR + InMatrix.m[4] * ScopedG + InMatrix.m[5] * ScopedB;
    const float ScopedCorrectedB = InMatrix.m[6] * ScopedR + InMatrix.m[7] * ScopedG + InMatrix.m[8] * ScopedB;

    const float ScopedOutR = ScopedR + (ScopedCorrectedR - ScopedR) * ScopedBlend;
    const float ScopedOutG = ScopedG + (ScopedCorrectedG - ScopedG) * ScopedBlend;
    const float ScopedOutB = ScopedB + (ScopedCorrectedB - ScopedB) * ScopedBlend;
    return Color32::FromFloats(LinearToSrgb(ScopedOutR), LinearToSrgb(ScopedOutG), LinearToSrgb(ScopedOutB),
                               static_cast<float>(InColor.A()) / 255.0f);
}

float SrgbRelativeLuminance(Color32 InColor) noexcept
{
    const float ScopedR = SrgbToLinear(static_cast<float>(InColor.R()) / 255.0f);
    const float ScopedG = SrgbToLinear(static_cast<float>(InColor.G()) / 255.0f);
    const float ScopedB = SrgbToLinear(static_cast<float>(InColor.B()) / 255.0f);
    return 0.2126f * ScopedR + 0.7152f * ScopedG + 0.0722f * ScopedB;
}

float ContrastRatio(Color32 InForeground, Color32 InBackground) noexcept
{
    const float ScopedFirst = SrgbRelativeLuminance(InForeground);
    const float ScopedSecond = SrgbRelativeLuminance(InBackground);
    const float ScopedBrighter = std::max(ScopedFirst, ScopedSecond);
    const float ScopedDarker = std::min(ScopedFirst, ScopedSecond);
    return (ScopedBrighter + 0.05f) / (ScopedDarker + 0.05f);
}

// FlashGuard
void FlashGuard::Configure(float InThreshold, float InResidualGain) noexcept
{
    MThreshold = std::clamp(InThreshold, 0.01f, 1.0f);
    MResidualGain = std::clamp(InResidualGain, 0.0f, 1.0f);
    Reset();
}
void FlashGuard::Reset() noexcept
{
    MPreviousLuminance = 0.0f;
    MGain = 1.0f;
    MPrimed = false;
}

float FlashGuard::Evaluate(float InMeanLuminance, float InDt) noexcept
{
    const float ScopedLuminance = std::clamp(InMeanLuminance, 0.0f, 1.0f);
    if (!MPrimed)
    {
        MPreviousLuminance = ScopedLuminance;
        MPrimed = true;
        MGain = 1.0f;
        return MGain;
    }

    const float ScopedDelta = std::fabs(ScopedLuminance - MPreviousLuminance);
    MPreviousLuminance = ScopedLuminance;
    if (ScopedDelta <= MThreshold)
    {
        MGain += (1.0f - MGain) * std::min(1.0f, std::max(InDt, 0.0f) * 8.0f);
        return MGain;
    }

    // Compress the delta to the threshold and add the residual floor. A full
    const float ScopedCompressed = MThreshold + (ScopedDelta - MThreshold) * MResidualGain;
    const float ScopedTarget = ScopedDelta > 0.0f ? std::min(1.0f, ScopedCompressed / ScopedDelta) : 1.0f;
    const float ScopedCoefficient = ScopedTarget < MGain ? 0.85f : 0.18f;
    MGain += (ScopedTarget - MGain) * std::clamp(std::max(InDt, 0.0f) * ScopedCoefficient * 60.0f, 0.0f, 1.0f);
    return std::clamp(MGain, 0.0f, 1.0f);
}

// NarrativeLog
bool NarrativeLog::Push(std::uint32_t InCode, Entity InEntity, Vector2D InPosition, std::string_view InText,
                        std::uint8_t InSeverity) noexcept
{
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_relaxed);
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    if (ScopedTail - ScopedHead >= kCapacity)
    {
        MDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    NarrativeEvent& ScopedEvent = MEvents[ScopedTail & (kCapacity - 1)];
    ScopedEvent = NarrativeEvent{};
    ScopedEvent.code = InCode;
    ScopedEvent.entity = InEntity;
    ScopedEvent.position = InPosition;
    ScopedEvent.severity = InSeverity;
    ScopedEvent.sequence = static_cast<std::uint32_t>(MSequence.fetch_add(1, std::memory_order_relaxed));
    const std::size_t ScopedLength = std::min<std::size_t>(InText.size(), kNarrativeTextBytes - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(ScopedEvent.text, InText.data(), ScopedLength);
    }
    ScopedEvent.textLength = static_cast<std::uint16_t>(ScopedLength);
    MTail.store(ScopedTail + 1, std::memory_order_release);
    return true;
}

bool NarrativeLog::Pop(NarrativeEvent& InEvent) noexcept
{
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_relaxed);
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    if (ScopedHead == ScopedTail)
    {
        return false;
    }
    InEvent = MEvents[ScopedHead & (kCapacity - 1)];
    MHead.store(ScopedHead + 1, std::memory_order_release);
    return true;
}

void NarrativeLog::Clear() noexcept
{
    MHead.store(0, std::memory_order_relaxed);
    MTail.store(0, std::memory_order_relaxed);
    MDropped.store(0, std::memory_order_relaxed);
}

std::uint32_t NarrativeLog::Count() const noexcept
{
    const std::uint32_t ScopedHead = MHead.load(std::memory_order_acquire);
    const std::uint32_t ScopedTail = MTail.load(std::memory_order_acquire);
    return ScopedTail - ScopedHead;
}

// SubtitleQueue
bool SubtitleQueue::Publish(const SoundTrigger& InTrigger, std::string_view InText) noexcept
{
    // Refresh an already visible cue instead of stacking duplicate captions.
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        if (MLines[ScopedI].cueId == InTrigger.cueId)
        {
            SubtitleLine& ScopedLine = MLines[ScopedI];
            ScopedLine.remaining = InTrigger.duration > 0.0f ? InTrigger.duration : 3.0f;
            ScopedLine.priority = InTrigger.priority;
            const std::size_t ScopedLength = std::min<std::size_t>(InText.size(), kNarrativeTextBytes - 1);
            std::memset(ScopedLine.text, 0, kNarrativeTextBytes);
            if (ScopedLength > 0)
            {
                std::memcpy(ScopedLine.text, InText.data(), ScopedLength);
            }
            ScopedLine.textLength = static_cast<std::uint16_t>(ScopedLength);
            return true;
        }
    }

    std::uint32_t ScopedSlot = MCount;
    if (ScopedSlot >= kCapacity)
    {
        // Replace the lowest-priority, oldest line. Accessibility output must
        ScopedSlot = 0;
        for (std::uint32_t ScopedI = 1; ScopedI < MCount; ++ScopedI)
        {
            if (MLines[ScopedI].priority < MLines[ScopedSlot].priority ||
                (MLines[ScopedI].priority == MLines[ScopedSlot].priority &&
                 MLines[ScopedI].remaining < MLines[ScopedSlot].remaining))
            {
                ScopedSlot = ScopedI;
            }
        }
        if (InTrigger.priority < MLines[ScopedSlot].priority)
        {
            ++MDropped;
            return false;
        }
    }
    else
    {
        ++MCount;
    }

    SubtitleLine& ScopedLine = MLines[ScopedSlot];
    ScopedLine = SubtitleLine{};
    ScopedLine.cueId = InTrigger.cueId;
    ScopedLine.source = InTrigger.source;
    ScopedLine.position = InTrigger.position;
    ScopedLine.priority = InTrigger.priority;
    ScopedLine.remaining = InTrigger.duration > 0.0f ? InTrigger.duration : 3.0f;
    const std::size_t ScopedLength = std::min<std::size_t>(InText.size(), kNarrativeTextBytes - 1);
    if (ScopedLength > 0)
    {
        std::memcpy(ScopedLine.text, InText.data(), ScopedLength);
    }
    ScopedLine.textLength = static_cast<std::uint16_t>(ScopedLength);
    return true;
}

void SubtitleQueue::Tick(float InDt) noexcept
{
    if (InDt <= 0.0f)
    {
        return;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount;)
    {
        MLines[ScopedI].remaining -= InDt;
        if (MLines[ScopedI].remaining <= 0.0f)
        {
            MLines[ScopedI] = MLines[MCount - 1];
            --MCount;
            continue;
        }
        ++ScopedI;
    }
}

void SubtitleQueue::Clear() noexcept
{
    MCount = 0;
    MDropped = 0;
    MLines.fill(SubtitleLine{});
}

// AccessibilityKernel
AccessibilityProfile DefaultAccessibilityProfile() noexcept
{
    AccessibilityProfile ScopedProfile;
    ScopedProfile.version = kAccessibilityProfileVersion;
    ScopedProfile.runtime.flags = AccessibilityFlashGuard;
    ScopedProfile.runtime.colorVision = ColorVisionDeficiency::None;
    ScopedProfile.runtime.colorVisionStrength = 1.0f;
    ScopedProfile.runtime.flashThreshold = 0.20f;
    ScopedProfile.runtime.flashGain = 0.35f;
    ScopedProfile.runtime.showSubtitles = false;
    ScopedProfile.presentation = AccessibilityPresentationSettings{};
    ScopedProfile.editor = AccessibilityEditorSettings{};
    return ScopedProfile;
}

void AccessibilityKernel::ApplyProfile(const AccessibilityProfile& InProfile) noexcept
{
    MProfile = InProfile;
    MProfile.version = kAccessibilityProfileVersion;
    MProfile.runtime.colorVisionStrength = std::clamp(MProfile.runtime.colorVisionStrength, 0.0f, 1.0f);
    MProfile.runtime.flashThreshold = std::clamp(MProfile.runtime.flashThreshold, 0.02f, 0.45f);
    MProfile.runtime.flashGain = std::clamp(MProfile.runtime.flashGain, 0.05f, 1.0f);
    MProfile.presentation.motionScale = std::clamp(MProfile.presentation.motionScale, 0.0f, 1.0f);
    MProfile.presentation.oledIntervalSeconds = std::clamp(MProfile.presentation.oledIntervalSeconds, 1.0f, 3600.0f);
    MProfile.presentation.hudFadeAfterIdleSeconds =
        std::clamp(MProfile.presentation.hudFadeAfterIdleSeconds, 0.0f, 600.0f);
    MProfile.presentation.minimumContrastRatio =
        std::clamp(MProfile.presentation.minimumContrastRatio, 1.0f, 21.0f);

    MSettings = MProfile.runtime;
    MGuard.Configure(MSettings.flashThreshold, MSettings.flashGain);
}

void AccessibilityKernel::SetBlindMode(bool InEnabled) noexcept
{
    // Hard main-thread contract. Enabling BlindMode may attach the native TTS
    if (!RequireMainThread("AccessibilityKernel::SetBlindMode"))
    {
        return;
    }
    if (InEnabled)
    {
        MSettings.flags |= AccessibilityBlindMode;
        // Native speech is what makes BlindMode usable without a game-side
        if (MTtsBackend == nullptr)
        {
            MTtsBackend = NativeTtsBackend();
            MNativeTtsAttached = MTtsBackend != nullptr;
        }
        if (!MMainThreadKnown)
        {
            MMainThread = MainThreadId();
            MMainThreadKnown = true;
        }
        ApplyTtsLanguage();
    }
    else
    {
        MSettings.flags &= ~AccessibilityBlindMode;
        if (MTtsBackend != nullptr && MTtsBackend->stop != nullptr)
        {
            MTtsBackend->stop(MTtsBackend->user);
        }
        if (!MExternalTts)
        {
            MTtsBackend = nullptr;
        }
        MNativeTtsAttached = false;
    }
}

void AccessibilityKernel::SetNarrativeLocale(std::string_view InLocale) noexcept
{
    MNarrative.SetLocaleFromCode(InLocale);
    if (InLocale.size() >= 2 && (InLocale[0] == 'r' || InLocale[0] == 'R') &&
        (InLocale[1] == 'u' || InLocale[1] == 'U'))
    {
        std::snprintf(MTtsLanguage, sizeof(MTtsLanguage), "%s", "ru-RU");
    }
    else
    {
        std::snprintf(MTtsLanguage, sizeof(MTtsLanguage), "%s", "en-US");
    }
    // The narrative tree itself is thread-agnostic, but voice selection is not.
    if (IsMainThread())
    {
        ApplyTtsLanguage();
    }
    else
    {
        MPendingVoiceLanguage = true;
    }
}

void AccessibilityKernel::ApplyTtsLanguage() noexcept
{
    if (MTtsBackend == nullptr || MTtsBackend->setVoice == nullptr)
    {
        MPendingVoiceLanguage = false;
        return;
    }
    if (!RequireMainThread("AccessibilityKernel::ApplyTtsLanguage"))
    {
        // Keep the request pending: FlushSpeech() runs on the owner thread and
        MPendingVoiceLanguage = true;
        return;
    }
    MTtsBackend->setVoice(MTtsBackend->user, MTtsLanguage);
    MPendingVoiceLanguage = false;
}

void AccessibilityKernel::NarrateScene(Ecs::Registry& InRegistry) noexcept
{
    MNarrative.Build(InRegistry);
    char ScopedBuffer[kNarrativeTextBytes]{};
    (void)MNarrative.DescribeScene(InRegistry, ScopedBuffer, sizeof(ScopedBuffer));
    PublishNarrative(200, entt::null, Vector2D{}, std::string_view(ScopedBuffer), 0);
    const std::uint32_t ScopedLimit = std::min<std::uint32_t>(MNarrative.NodeCount(), 16);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedLimit; ++ScopedIndex)
    {
        const Ui::NarrativeNode* ScopedNode = MNarrative.FindByIndex(ScopedIndex);
        if (ScopedNode == nullptr)
        {
            continue;
        }
        (void)MNarrative.DescribeEntity(InRegistry, ScopedNode->entity, ScopedBuffer, sizeof(ScopedBuffer));
        const auto* ScopedTransform = InRegistry.try_get<Transform2D>(ScopedNode->entity);
        PublishNarrative(201, ScopedNode->entity,
                         ScopedTransform != nullptr ? ScopedTransform->Position : Vector2D{},
                         std::string_view(ScopedBuffer), 0);
    }
}

bool AccessibilityKernel::BlindMode() const noexcept
{
    return (MSettings.flags & AccessibilityBlindMode) != 0;
}

void AccessibilityKernel::SetDeafMode(bool InEnabled) noexcept
{
    if (InEnabled)
    {
        MSettings.flags |= AccessibilityDeafMode;
    }
    else
    {
        MSettings.flags &= ~AccessibilityDeafMode;
    }
}

bool AccessibilityKernel::DeafMode() const noexcept
{
    return (MSettings.flags & AccessibilityDeafMode) != 0;
}

void AccessibilityKernel::SetShowSubtitles(bool InEnabled) noexcept
{
    MSettings.showSubtitles = InEnabled;
}

void AccessibilityKernel::SetFlashGuard(bool InEnabled, float InThreshold, float InGain) noexcept
{
    if (InEnabled)
    {
        MSettings.flags |= AccessibilityFlashGuard;
        MSettings.flashThreshold = std::clamp(InThreshold, 0.01f, 1.0f);
        MSettings.flashGain = std::clamp(InGain, 0.0f, 1.0f);
        MGuard.Configure(MSettings.flashThreshold, MSettings.flashGain);
    }
    else
    {
        MSettings.flags &= ~AccessibilityFlashGuard;
        MGuard.Reset();
    }
}

void AccessibilityKernel::SetColorVision(ColorVisionDeficiency InDeficiency, float InStrength) noexcept
{
    MSettings.colorVision = InDeficiency;
    MSettings.colorVisionStrength = std::clamp(InStrength, 0.0f, 1.0f);
    if (InDeficiency == ColorVisionDeficiency::None)
    {
        MSettings.flags &= ~AccessibilityColorVisionMode;
    }
    else
    {
        MSettings.flags |= AccessibilityColorVisionMode;
    }
}

void AccessibilityKernel::ApplyToFrame(Rhi::FramePacket& InPacket) noexcept
{
    InPacket.accessibility.flags = 0;
    if (BlindMode())
    {
        InPacket.accessibility.flags |= Rhi::AccessibilityBlind;
    }
    if (DeafMode())
    {
        InPacket.accessibility.flags |= Rhi::AccessibilityDeaf;
    }
    if ((MSettings.flags & AccessibilityFlashGuard) != 0)
    {
        InPacket.accessibility.flags |= Rhi::AccessibilityFlashGuard;
    }
    if ((MSettings.flags & AccessibilityColorVisionMode) != 0)
    {
        InPacket.accessibility.flags |= Rhi::AccessibilityColorVision;
    }
    InPacket.accessibility.colorVision = static_cast<std::uint32_t>(MSettings.colorVision);
    InPacket.accessibility.colorVisionStrength = MSettings.colorVisionStrength;
    InPacket.accessibility.flashThreshold = MSettings.flashThreshold;
    InPacket.accessibility.flashGain = MSettings.flashGain;
}

void AccessibilityKernel::ApplyCpuColorFallback(std::span<Rhi::RenderItem> InItems, Color32& InClear) const noexcept
{
    const bool ScopedVision = AccessibilityHasFlag(MSettings.flags, AccessibilityColorVisionMode);
    const bool ScopedGuard = AccessibilityHasFlag(MSettings.flags, AccessibilityFlashGuard);
    if (!ScopedVision && !ScopedGuard)
    {
        return;
    }
    const ColorMatrix3 ScopedMatrix =
        ScopedVision ? ColorVisionMatrix(MSettings.colorVision) : ColorMatrix3{};
    const float ScopedStrength = std::clamp(MSettings.colorVisionStrength, 0.0f, 1.0f);
    const float ScopedGain = ScopedGuard ? std::clamp(MGuard.CurrentGain(), 0.0f, 1.0f) : 1.0f;
    const auto ScopedCorrect = [&](Color32 InColor) noexcept
    {
        Color32 ScopedResult = InColor;
        if (ScopedVision)
        {
            ScopedResult = ApplyColorVision(ScopedResult, ScopedMatrix, ScopedStrength);
        }
        if (ScopedGuard && ScopedGain < 1.0f)
        {
            const auto ScopedScale = [ScopedGain](std::uint8_t InValue) noexcept
            {
                return static_cast<std::uint8_t>(std::lround(static_cast<float>(InValue) * ScopedGain));
            };
            ScopedResult = Color32::FromBytes(ScopedScale(ScopedResult.R()), ScopedScale(ScopedResult.G()),
                                              ScopedScale(ScopedResult.B()), ScopedResult.A());
        }
        return ScopedResult;
    };

    InClear = ScopedCorrect(InClear);
    for (Rhi::RenderItem& ScopedItem : InItems)
    {
        ScopedItem.color = ScopedCorrect(Color32{ScopedItem.color}).rgba;
    }
}

void AccessibilityKernel::ApplyPostFx(Rhi::PostFxSettings& InSettings) const noexcept
{
    InSettings.flags &= ~(static_cast<std::uint32_t>(Rhi::PostFxFlashGuard) |
                          static_cast<std::uint32_t>(Rhi::PostFxColorVision));
    if (AccessibilityHasFlag(MSettings.flags, AccessibilityFlashGuard))
    {
        InSettings.flags |= Rhi::PostFxFlashGuard;
        // The guard's live gain, not the configured residual: it is 1.0 until a
        // flash is actually detected, so the pass never dims a calm frame.
        InSettings.flashGuardGain = std::clamp(MGuard.CurrentGain(), 0.0f, 1.0f);
    }
    if (AccessibilityHasFlag(MSettings.flags, AccessibilityColorVisionMode))
    {
        InSettings.flags |= Rhi::PostFxColorVision;
        InSettings.colorVision = std::min<std::uint32_t>(static_cast<std::uint32_t>(MSettings.colorVision), 3u);
        InSettings.colorVisionStrength = std::clamp(MSettings.colorVisionStrength, 0.0f, 1.0f);
    }
}

AccessibilityKernel::StateTrack* AccessibilityKernel::FindTrack(Entity InEntity) noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MTrackCount; ++ScopedI)
    {
        if (MTracks[ScopedI].entity == InEntity)
        {
            return &MTracks[ScopedI];
        }
    }
    return nullptr;
}

void AccessibilityKernel::TrackGameplayState(Ecs::Registry& InRegistry) noexcept
{
    auto ScopedView = InRegistry.view<GameplayState, Transform2D>();
    for (auto [ScopedEntity, ScopedState, ScopedTransform] : ScopedView.each())
    {
        StateTrack* ScopedTrack = FindTrack(ScopedEntity);
        if (ScopedTrack == nullptr)
        {
            if (MTrackCount >= MTracks.size())
            {
                continue;
            }
            ScopedTrack = &MTracks[MTrackCount++];
            ScopedTrack->entity = ScopedEntity;
            ScopedTrack->bits = 0;
            char ScopedBuffer[kNarrativeTextBytes]{};
            (void)MNarrative.DescribeEntity(InRegistry, ScopedEntity, ScopedBuffer, sizeof(ScopedBuffer));
            PublishNarrative(1, ScopedEntity, ScopedTransform.Position, ScopedBuffer);
        }

        const GameplayStateMask ScopedChanged = ScopedState.Bits ^ ScopedTrack->bits;
        if (ScopedChanged == 0)
        {
            continue;
        }
        for (std::uint32_t ScopedBit = 0; ScopedBit < 16; ++ScopedBit)
        {
            const GameplayStateMask ScopedMask = 1ull << ScopedBit;
            if ((ScopedChanged & ScopedMask) == 0)
            {
                continue;
            }
            char ScopedBuffer[kNarrativeTextBytes]{};
            (void)MNarrative.DescribeStateChange(InRegistry, ScopedEntity, ScopedBit, (ScopedState.Bits & ScopedMask) != 0,
                                                ScopedBuffer, sizeof(ScopedBuffer));
            PublishNarrative(2, ScopedEntity, ScopedTransform.Position, ScopedBuffer);
        }
        ScopedTrack->bits = ScopedState.Bits;
    }
}

void AccessibilityKernel::EmitContacts(Ecs::SystemContext& InContext) noexcept
{
    if (InContext.physics == nullptr)
    {
        return;
    }
    for (const PhysicsContactEvent& ScopedContact : InContext.physics->Contacts())
    {
        char ScopedBuffer[kNarrativeTextBytes]{};
        (void)MNarrative.DescribeContact(InContext.registry, ScopedContact.a, ScopedContact.b,
                                         static_cast<std::uint32_t>(ScopedContact.type), ScopedContact.speed,
                                         ScopedBuffer, sizeof(ScopedBuffer));
        PublishNarrative(100 + static_cast<std::uint32_t>(ScopedContact.type), ScopedContact.a, ScopedContact.point,
                         ScopedBuffer, ScopedContact.type == PhysicsEventType::Hit ? 2 : 0);
    }
}

void AccessibilityKernel::Update(Ecs::SystemContext& InContext) noexcept
{
    AdvanceCaptionClock(InContext.dt);
    PollVoiceCaptions();
    if (BlindMode())
    {
        MNarrative.Build(InContext.registry);
        TrackGameplayState(InContext.registry);
        EmitContacts(InContext);
    // Speech is deliberately not flushed here: Update() is the simulation
    }

    if (SubtitlesVisible())
    {
        MSubtitles.Tick(InContext.dt > 0.0f ? InContext.dt : 0.0f);
    }
    if (DeafMode())
    {
        if (InContext.audio != nullptr && !MAudioMuted)
        {
            MAudioWasMuted = InContext.audio->MasterMuted();
            InContext.audio->SetMasterMuted(true);
            MAudioMuted = true;
        }
    }
    else if (MAudioMuted && InContext.audio != nullptr)
    {
        // Only undo a mute this kernel applied; a game-initiated master mute
        if (!MAudioWasMuted)
        {
            InContext.audio->SetMasterMuted(false);
        }
        MAudioMuted = false;
        MAudioWasMuted = false;
    }
}

void AccessibilityKernel::PublishNarrative(std::uint32_t InCode, Entity InEntity, Vector2D InPosition,
                                           std::string_view InText, std::uint8_t InSeverity) noexcept
{
    (void)MLog.Push(InCode, InEntity, InPosition, InText, InSeverity);
}

void AccessibilityKernel::SetSoundCues(const SoundCueRegistry* InRegistry) noexcept
{
    MSoundCues = InRegistry;
}

void AccessibilityKernel::SetCueTextResolver(std::string_view (*InResolver)(void*, std::uint32_t) noexcept,
                                             void* InUser) noexcept
{
    MCueTextResolver = InResolver;
    MCueTextUser = InUser;
}

namespace
{
std::string_view LocalizedCueText(void* InUser, std::uint32_t InLocId) noexcept
{
    const auto* ScopedTable = static_cast<const LocalizationTable*>(InUser);
    return ScopedTable != nullptr ? ScopedTable->FindWithFallback(InLocId) : std::string_view{};
}
}

void AccessibilityKernel::SetCueLocalization(const LocalizationTable* InTable) noexcept
{
    MCueTextResolver = &LocalizedCueText;
    MCueTextUser = const_cast<LocalizationTable*>(InTable);
}

void AccessibilityKernel::SetVoiceCaptionProvider(const VoiceCaptionProvider* InProvider) noexcept
{
    MVoiceCaptionProvider = InProvider;
}

void AccessibilityKernel::AdvanceCaptionClock(float InDeltaSeconds) noexcept
{
    if (InDeltaSeconds > 0.0f)
    {
        MCaptionClockMs += InDeltaSeconds * 1000.0f;
    }
}

void AccessibilityKernel::PublishCueSound(std::uint32_t InCueId, const SoundTrigger& InTrigger) noexcept
{
    if (MSoundCues == nullptr)
    {
        return;
    }
    const SoundCueDef* ScopedCue = MSoundCues->Find(InCueId);
    if (ScopedCue == nullptr)
    {
        return;
    }
    if (!MSoundCues->TryTrigger(InCueId, static_cast<std::uint64_t>(MCaptionClockMs)))
    {
        return;
    }

    std::string_view ScopedText;
    if (MCueTextResolver != nullptr)
    {
        ScopedText = MCueTextResolver(MCueTextUser, ScopedCue->LocId);
    }
    if (ScopedText.empty())
    {
        ScopedText = "sound";
    }

    SoundTrigger ScopedTrigger = InTrigger;
    ScopedTrigger.cueId = InCueId;
    ScopedTrigger.priority = ScopedCue->Priority;
    if (SubtitlesVisible())
    {
        MSubtitles.Publish(ScopedTrigger, ScopedText);
    }
    if (BlindMode())
    {
        PublishNarrative(InCueId, ScopedTrigger.source, ScopedTrigger.position, ScopedText, ScopedCue->Priority);
    }
}

void AccessibilityKernel::PollVoiceCaptions() noexcept
{
    if (MVoiceCaptionProvider == nullptr || MVoiceCaptionProvider->Poll == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        VoiceCaptionEvent ScopedEvent{};
        if (!MVoiceCaptionProvider->Poll(MVoiceCaptionProvider->User, &ScopedEvent))
        {
            break;
        }
        if (ScopedEvent.Text.empty())
        {
            continue;
        }
        if (SubtitlesVisible())
        {
            SoundTrigger ScopedTrigger{};
            ScopedTrigger.cueId = VoiceCaptionCueId(ScopedEvent.PlayerId);
            ScopedTrigger.priority = ScopedEvent.Priority;
            ScopedTrigger.duration = 3.0f;
            MSubtitles.Publish(ScopedTrigger, ScopedEvent.Text);
        }
        if (BlindMode())
        {
            PublishNarrative(ScopedEvent.CueId, entt::null, {}, ScopedEvent.Text, ScopedEvent.Priority);
        }
    }
}

void AccessibilityKernel::PublishSoundTrigger(const SoundTrigger& InTrigger, std::string_view InText) noexcept
{
    if (!SubtitlesVisible())
    {
        return;
    }
    if (InText.empty())
    {
        char ScopedBuffer[64]{};
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "[audio cue %u]", InTrigger.cueId);
        (void)MSubtitles.Publish(InTrigger, ScopedBuffer);
        return;
    }
    (void)MSubtitles.Publish(InTrigger, InText);
}

void AccessibilityKernel::ProcessTimeline(std::span<const TimedSoundCue> InCues, float InPreviousTime,
                                          float InCurrentTime) noexcept
{
    if (!SubtitlesVisible() || InCurrentTime <= InPreviousTime)
    {
        return;
    }
    for (const TimedSoundCue& ScopedCue : InCues)
    {
        if (ScopedCue.timeSeconds <= InPreviousTime || ScopedCue.timeSeconds > InCurrentTime)
        {
            continue;
        }
        PublishSoundTrigger(ScopedCue.trigger, std::string_view{ScopedCue.text, ScopedCue.textLength});
    }
}

void AccessibilityKernel::SetSpeechSink(SpeechSink InSink, void* InUser) noexcept
{
    MSpeechSink = InSink;
    MSpeechUser = InUser;
}

void AccessibilityKernel::SetTtsBackend(const TtsBackend* InBackend) noexcept
{
    // Installing a backend is a main-thread operation for the same reason as
    if (!RequireMainThread("AccessibilityKernel::SetTtsBackend"))
    {
        return;
    }
    MTtsBackend = InBackend;
    MNativeTtsAttached = false;
    MExternalTts = InBackend != nullptr;
    if (!MMainThreadKnown)
    {
        // Seed from the process main thread, never from "whoever called first":
        MMainThread = MainThreadId();
        MMainThreadKnown = true;
    }
    ApplyTtsLanguage();
}

void AccessibilityKernel::FlushSpeech() noexcept
{
    // Hard main-thread contract. SAPI is apartment-threaded and AVSpeech owns a
    const std::thread::id ScopedCurrentThread = std::this_thread::get_id();
    if (!MMainThreadKnown)
    {
        // The anchor comes from the process, not from this call: otherwise the
        MMainThread = MainThreadId();
        MMainThreadKnown = true;
    }
    if (ScopedCurrentThread != MMainThread || !IsMainThread())
    {
        return;
    }
    // A locale change from a worker is applied here, on the owner thread, before
    if (MPendingVoiceLanguage)
    {
        ApplyTtsLanguage();
    }
    if (MTtsBackend == nullptr || MTtsBackend->speak == nullptr)
    {
        if (MSpeechSink == nullptr)
        {
            return;
        }
        NarrativeEvent ScopedEvent{};
        while (MLog.Pop(ScopedEvent))
        {
            MSpeechSink(MSpeechUser, ScopedEvent.text, ScopedEvent.textLength);
        }
        return;
    }
    NarrativeEvent ScopedEvent{};
    while (MLog.Pop(ScopedEvent))
    {
        const TtsPriority ScopedPriority =
            ScopedEvent.severity >= 2 ? TtsPriority::Interrupt
                                      : (ScopedEvent.severity == 1 ? TtsPriority::Normal : TtsPriority::Queued);
        (void)MTtsBackend->speak(MTtsBackend->user, ScopedEvent.text, ScopedEvent.textLength, ScopedPriority);
    }
}

bool AccessibilityKernel::BuildSubtitleCanvas(Ui::WidgetCanvas& InCanvas) const noexcept
{
    const Vector2D ScopedPreservedSize = InCanvas.size;
    InCanvas.Clear();
    InCanvas.SetCanvasSize(ScopedPreservedSize);
    if (!SubtitlesVisible() || MSubtitles.Count() == 0)
    {
        return false;
    }

    const std::uint32_t ScopedVisible = std::min(MSubtitles.Count(), 3u);
    constexpr float ScopedKLineHeight = 34.0f;
    constexpr float ScopedKPadding = 12.0f;
    const float ScopedHeight = ScopedKLineHeight * static_cast<float>(ScopedVisible) + ScopedKPadding * 2.0f;
    const float ScopedWidth = std::min(ScopedPreservedSize.X * 0.8f, 1200.0f);
    const float ScopedLeft = (ScopedPreservedSize.X - ScopedWidth) * 0.5f;
    const float ScopedBottom = ScopedPreservedSize.Y - 48.0f;
    const float ScopedTop = ScopedBottom - ScopedHeight;

    Ui::WidgetDesc ScopedPanel{};
    ScopedPanel.kind = Ui::WidgetKind::Panel;
    ScopedPanel.anchorMin = Vector2D{0.0f, 0.0f};
    ScopedPanel.anchorMax = Vector2D{0.0f, 0.0f};
    ScopedPanel.offsetMin = Vector2D{ScopedLeft, ScopedTop};
    ScopedPanel.offsetMax = Vector2D{ScopedLeft + ScopedWidth, ScopedBottom};
    ScopedPanel.tint = Color32::FromBytes(0x0C, 0x0D, 0x10, 0xDC);
    ScopedPanel.zOrder = 0;
    ScopedPanel.flags = static_cast<std::uint32_t>(Ui::WidgetFlags::Visible) |
                        static_cast<std::uint32_t>(Ui::WidgetFlags::Enabled) |
                        static_cast<std::uint32_t>(Ui::WidgetFlags::IgnoreHitTest);
    (void)InCanvas.AddWidget(ScopedPanel);

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedVisible; ++ScopedI)
    {
        const SubtitleLine& ScopedLine = MSubtitles.At(ScopedI);
        Ui::WidgetDesc ScopedLabel{};
        ScopedLabel.kind = Ui::WidgetKind::Label;
        ScopedLabel.anchorMin = Vector2D{0.0f, 0.0f};
        ScopedLabel.anchorMax = Vector2D{0.0f, 0.0f};
        ScopedLabel.offsetMin =
            Vector2D{ScopedLeft + 16.0f, ScopedTop + ScopedKPadding + static_cast<float>(ScopedI) * ScopedKLineHeight};
        ScopedLabel.offsetMax =
            Vector2D{ScopedLeft + ScopedWidth - 16.0f,
                     ScopedTop + ScopedKPadding + static_cast<float>(ScopedI + 1) * ScopedKLineHeight};
        ScopedLabel.tint = Color32::White();
        ScopedLabel.fontSize = 20;
        ScopedLabel.zOrder = 1;
        ScopedLabel.text = std::string_view{ScopedLine.text, ScopedLine.textLength};
        ScopedLabel.flags = static_cast<std::uint32_t>(Ui::WidgetFlags::Visible) |
                            static_cast<std::uint32_t>(Ui::WidgetFlags::Enabled) |
                            static_cast<std::uint32_t>(Ui::WidgetFlags::IgnoreHitTest);
        (void)InCanvas.AddWidget(ScopedLabel);
    }
    return true;
}
}
