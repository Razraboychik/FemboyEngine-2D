// FemboyEngine2D - sound cue registry and caption integration tests.
#include "Engine/Assets/Localization.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/SoundCues.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace
{
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

std::string_view ResolveCue(void*, std::uint32_t InLocId) noexcept
{
    if (InLocId == 100)
    {
        return "glass breaks";
    }
    if (InLocId == 200)
    {
        return "footstep";
    }
    return {};
}

struct CaptionFeed
{
    std::uint32_t Index = 0;
};

bool PollCaption(void* InUser, Fby::VoiceCaptionEvent* OutEvent) noexcept
{
    auto* ScopedFeed = static_cast<CaptionFeed*>(InUser);
    if (ScopedFeed->Index == 0)
    {
        ++ScopedFeed->Index;
        OutEvent->PlayerId = 1;
        OutEvent->CueId = 9001;
        OutEvent->Priority = 3;
        OutEvent->Text = "Player 1: alpha";
        return true;
    }
    if (ScopedFeed->Index == 1)
    {
        ++ScopedFeed->Index;
        OutEvent->PlayerId = 1;
        OutEvent->CueId = 9002;
        OutEvent->Priority = 3;
        OutEvent->Text = "Player 1: bravo";
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view LineText(const Fby::SubtitleLine& InLine)
{
    return std::string_view(InLine.text, InLine.textLength);
}

void TestRegistry()
{
    using namespace Fby;

    SoundCueRegistry ScopedRegistry;
    SoundCueDef ScopedImpact{};
    ScopedImpact.CueId = 10;
    ScopedImpact.LocId = 100;
    ScopedImpact.Category = SoundCueCategory::Impact;
    ScopedImpact.Priority = 2;
    ScopedImpact.CooldownMs = 50;
    FBY_CHECK(ScopedRegistry.Add(ScopedImpact));
    FBY_CHECK(!ScopedRegistry.Add(ScopedImpact));

    SoundCueDef ScopedFoot{};
    ScopedFoot.CueId = 20;
    ScopedFoot.LocId = 200;
    ScopedFoot.Category = SoundCueCategory::Footstep;
    ScopedFoot.CooldownMs = 500;
    FBY_CHECK(ScopedRegistry.Add(ScopedFoot));
    FBY_CHECK(ScopedRegistry.Count() == 2);

    FBY_CHECK(ScopedRegistry.Find(10) != nullptr);
    FBY_CHECK(ScopedRegistry.Find(11) == nullptr);
    FBY_CHECK(std::strcmp(SoundCueCategoryName(ScopedRegistry.Find(20)->Category), "footstep") == 0);

    FBY_CHECK(ScopedRegistry.TryTrigger(10, 0));
    FBY_CHECK(!ScopedRegistry.TryTrigger(10, 10));
    FBY_CHECK(ScopedRegistry.TryTrigger(10, 50));
    FBY_CHECK(!ScopedRegistry.TryTrigger(999, 100));

    ScopedRegistry.ResetCooldowns();
    FBY_CHECK(ScopedRegistry.TryTrigger(10, 0));

    // The voice-caption namespace is reserved: authored cues cannot enter it.
    SoundCueDef ScopedReserved{};
    ScopedReserved.CueId = kVoiceCaptionCueFirst + 1;
    ScopedReserved.LocId = 300;
    FBY_CHECK(!ScopedRegistry.Add(ScopedReserved));
    FBY_CHECK(IsVoiceCaptionCueId(VoiceCaptionCueId(1)));
    FBY_CHECK(VoiceCaptionCueId(1) == kVoiceCaptionCueFirst + 1);
    FBY_CHECK(!IsVoiceCaptionCueId(10));
}

void TestCaptionIntegration()
{
    using namespace Fby;

    SoundCueRegistry ScopedRegistry;
    SoundCueDef ScopedImpact{};
    ScopedImpact.CueId = 10;
    ScopedImpact.LocId = 100;
    ScopedImpact.Priority = 2;
    ScopedImpact.CooldownMs = 50;
    ScopedImpact.Category = SoundCueCategory::Impact;
    FBY_CHECK(ScopedRegistry.Add(ScopedImpact));

    SoundCueDef ScopedFoot{};
    ScopedFoot.CueId = 20;
    ScopedFoot.LocId = 200;
    ScopedFoot.Priority = 1;
    ScopedFoot.CooldownMs = 100;
    ScopedFoot.Category = SoundCueCategory::Footstep;
    FBY_CHECK(ScopedRegistry.Add(ScopedFoot));

    AccessibilityKernel ScopedKernel;
    ScopedKernel.SetSoundCues(&ScopedRegistry);
    ScopedKernel.SetCueTextResolver(&ResolveCue, nullptr);

    SoundTrigger ScopedTrigger{};
    ScopedTrigger.duration = 2.0f;

    // No DeafMode and no showSubtitles means no cue caption at all.
    ScopedKernel.PublishCueSound(10, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == 0);

    // The independent showSubtitles switch is enough (game audio still plays).
    ScopedKernel.SetShowSubtitles(true);
    ScopedKernel.AdvanceCaptionClock(0.06f);
    ScopedKernel.PublishCueSound(10, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == 1);
    FBY_CHECK(LineText(ScopedKernel.Subtitles().At(0)) == "glass breaks");
    ScopedKernel.Subtitles().Clear();
    ScopedKernel.SetShowSubtitles(false);

    ScopedKernel.SetDeafMode(true);
    ScopedKernel.AdvanceCaptionClock(0.06f);
    ScopedKernel.PublishCueSound(10, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == 1);
    FBY_CHECK(LineText(ScopedKernel.Subtitles().At(0)) == "glass breaks");
    FBY_CHECK(ScopedKernel.Subtitles().At(0).priority == 2);

    // Cooldown suppresses the immediate repeat; a different cue adds a line.
    ScopedKernel.PublishCueSound(10, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == 1);
    ScopedKernel.AdvanceCaptionClock(0.06f);
    ScopedKernel.PublishCueSound(20, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == 2);
    FBY_CHECK(LineText(ScopedKernel.Subtitles().At(1)) == "footstep");

    // BlindMode mirrors the cue into the narrative log.
    ScopedKernel.SetBlindMode(true);
    ScopedKernel.AdvanceCaptionClock(0.06f);
    ScopedKernel.PublishCueSound(10, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Log().Count() >= 1);

    // Voice captions reach the subtitle queue while DeafMode is on.
    CaptionFeed ScopedFeed{};
    VoiceCaptionProvider ScopedProvider{};
    ScopedProvider.User = &ScopedFeed;
    ScopedProvider.Poll = &PollCaption;
    ScopedKernel.SetVoiceCaptionProvider(&ScopedProvider);
    const std::uint32_t ScopedBefore = ScopedKernel.Subtitles().Count();
    ScopedKernel.PollVoiceCaptions();
    // Both events belong to player 1, so the second one refreshes the same
    // 0x4001 caption instead of adding a duplicate line.
    FBY_CHECK(ScopedKernel.Subtitles().Count() == ScopedBefore + 1);
    FBY_CHECK(LineText(ScopedKernel.Subtitles().At(ScopedBefore)) == "Player 1: bravo");
    // Voice captions live in their own namespace: 0x4000 + player id.
    FBY_CHECK(ScopedKernel.Subtitles().At(ScopedBefore).cueId == kVoiceCaptionCueFirst + 1);

    // Without DeafMode voice captions are not queued.
    ScopedKernel.SetDeafMode(false);
    ScopedFeed.Index = 0;
    const std::uint32_t ScopedQuiet = ScopedKernel.Subtitles().Count();
    ScopedKernel.PollVoiceCaptions();
    FBY_CHECK(ScopedKernel.Subtitles().Count() == ScopedQuiet);

    // Unknown cue ids are ignored.
    ScopedKernel.SetDeafMode(true);
    ScopedKernel.PublishCueSound(777, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == ScopedQuiet);

    // The localization adapter is the default resolver. An invalid table
    // yields an empty string, so the cue still shows a generic caption.
    SoundCueDef ScopedAdapterCue{};
    ScopedAdapterCue.CueId = 30;
    ScopedAdapterCue.LocId = 100;
    FBY_CHECK(ScopedRegistry.Add(ScopedAdapterCue));
    LocalizationTable ScopedTable;
    ScopedKernel.SetCueLocalization(&ScopedTable);
    ScopedKernel.SetDeafMode(true);
    ScopedKernel.AdvanceCaptionClock(0.5f);
    const std::uint32_t ScopedLocalizedBefore = ScopedKernel.Subtitles().Count();
    ScopedKernel.PublishCueSound(30, ScopedTrigger);
    FBY_CHECK(ScopedKernel.Subtitles().Count() == ScopedLocalizedBefore + 1);
    FBY_CHECK(LineText(ScopedKernel.Subtitles().At(ScopedLocalizedBefore)) == "sound");
}
}

int main()
{
    TestRegistry();
    TestCaptionIntegration();

    if (GFailures == 0)
    {
        std::printf("sound_cues_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sound_cues_test: %u failure(s)\n", GFailures);
    return 1;
}
