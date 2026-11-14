// FemboyEngine2D - data-driven action narration tests.
#include "Engine/Assets/Localization.hpp"
#include "Engine/Assets/LocalizationCompiler.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/ActionNarration.hpp"
#include "Engine/UI/Narrator.hpp"
#include "Engine/UI/Tts.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

std::uint32_t GSpeakCount = 0;
char GLastSpoken[512]{};

bool TestSpeak(void*, const char* InText, std::size_t InLength, Fby::TtsPriority) noexcept
{
    ++GSpeakCount;
    const std::size_t ScopedLength = InLength < sizeof(GLastSpoken) - 1 ? InLength : sizeof(GLastSpoken) - 1;
    std::memcpy(GLastSpoken, InText, ScopedLength);
    GLastSpoken[ScopedLength] = '\0';
    return true;
}

void TestNoop(void*) noexcept
{
}

void TestSetRate(void*, float) noexcept
{
}

void TestSetVoice(void*, const char*) noexcept
{
}

bool TestSetVoiceById(void*, const char*) noexcept
{
    return false;
}

std::uint32_t TestEnumerate(void*, Fby::TtsVoiceInfo*, std::uint32_t) noexcept
{
    return 0;
}

Fby::TtsBackend MakeBackend() noexcept
{
    Fby::TtsBackend ScopedBackend{};
    ScopedBackend.speak = &TestSpeak;
    ScopedBackend.stop = &TestNoop;
    ScopedBackend.setRate = &TestSetRate;
    ScopedBackend.setVoice = &TestSetVoice;
    ScopedBackend.setVoiceById = &TestSetVoiceById;
    ScopedBackend.enumerateVoices = &TestEnumerate;
    return ScopedBackend;
}

bool Contains(const char* InText, const char* InNeedle) noexcept
{
    return std::strstr(InText, InNeedle) != nullptr;
}
}

int main()
{
    using namespace Fby;

    // A pack that covers wording, plurals, spatial words and an unknown token.
    const std::vector<std::byte> ScopedBytes =
        Assets::CompileLocalization("default = \"en\"\n"
                                    "[en]\n"
                                    "1000 = \"You picked up {count} {object}\"\n"
                                    "1010 = \"{subject} is {direction}, {distance}\"\n"
                                    "1020 = \"Door opened\"\n"
                                    "1030 = \"{subject} picked up {count} {object}\"\n"
                                    "200 = \"Picked up {count} apples\"\n"
                                    "201 = \"Picked up one apple\"\n"
                                    "300 = \"ahead\"\n"
                                    "301 = \"ahead right\"\n"
                                    "302 = \"right\"\n"
                                    "303 = \"behind right\"\n"
                                    "304 = \"behind\"\n"
                                    "305 = \"behind left\"\n"
                                    "306 = \"left\"\n"
                                    "307 = \"ahead left\"\n"
                                    "310 = \"near\"\n"
                                    "311 = \"medium\"\n"
                                    "312 = \"far\"\n"
                                    "400 = \"Kira\"\n"
                                    "401 = \"apple\"\n"
                                    "1500 = \"Item {object}\"\n"
                                    "1501 = \"Unknown {token} token\"\n"
                                    "[ru]\n"
                                    "200 = \"\\u041F\\u043E\\u0434\\u043E\\u0431\\u0440\\u0430\\u043D\\u043E \\u044F\\u0431\\u043B\\u043E\\u043A: {count}\"\n"
                                    "201 = \"\\u041F\\u043E\\u0434\\u043E\\u0431\\u0440\\u0430\\u043D\\u043E \\u043E\\u0434\\u043D\\u043E \\u044F\\u0431\\u043B\\u043E\\u043A\\u043E\"\n"
                                    "202 = \"\\u041F\\u043E\\u0434\\u043E\\u0431\\u0440\\u0430\\u043D\\u043E {count} \\u044F\\u0431\\u043B\\u043E\\u043A\\u0430\"\n"
                                    "203 = \"\\u041F\\u043E\\u0434\\u043E\\u0431\\u0440\\u0430\\u043D\\u043E {count} \\u044F\\u0431\\u043B\\u043E\\u043A\"\n",
                                    nullptr);
    LocalizationTable ScopedTable = LocalizationTable::Open(ScopedBytes, nullptr);
    FBY_CHECK(ScopedTable.IsValid());

    ActionVocabulary ScopedVocabulary{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        ScopedVocabulary.Directions[ScopedIndex] = 300 + ScopedIndex;
    }
    ScopedVocabulary.Near = 310;
    ScopedVocabulary.Medium = 311;
    ScopedVocabulary.Far = 312;

    ActionNarration ScopedActions;
    ScopedActions.SetLocalization(&ScopedTable);
    ScopedActions.SetVocabulary(ScopedVocabulary);

    char ScopedLine[512]{};

    // Plain substitution: object name and count.
    ActionEvent ScopedEvent{};
    ScopedEvent.TemplateId = 1000;
    ScopedEvent.ObjectNameId = 401;
    ScopedEvent.Count = 1;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "You picked up 1 apple") == 0);

    // Direction and distance buckets: 90 degrees is "right", 2 m is "near".
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 1010;
    ScopedEvent.SubjectNameId = 400;
    ScopedEvent.AngleRadians = 1.5707964f;
    ScopedEvent.HasDirection = true;
    ScopedEvent.DistanceMeters = 2.0f;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "Kira is right, near") == 0);

    // Unknown template and a disabled layer compose nothing.
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 9999;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) == 0);
    ActionNarrationSettings ScopedSettings;
    ScopedSettings.Enabled = false;
    ScopedActions.SetSettings(ScopedSettings);
    ScopedEvent.TemplateId = 1020;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) == 0);
    ScopedActions.SetSettings(ActionNarrationSettings{});

    // A missing object name leaves a clean sentence, without double spaces.
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 1500;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "Item") == 0);

    // An unknown placeholder stays visible so an author spots the typo.
    ScopedEvent.TemplateId = 1501;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(Contains(ScopedLine, "{token}"));

    // English plural: one gets its own variant, the rest the "other" variant.
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 200;
    ScopedEvent.Count = 1;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "Picked up one apple") == 0);
    ScopedEvent.Count = 4;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "Picked up 4 apples") == 0);

    // Russian plural categories: one, few, many.
    FBY_CHECK(ScopedTable.SelectLocale("ru"));
    ScopedEvent.Count = 1;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "\u041F\u043E\u0434\u043E\u0431\u0440\u0430\u043D\u043E \u043E\u0434\u043D\u043E \u044F\u0431\u043B\u043E\u043A\u043E") == 0);
    ScopedEvent.Count = 3;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "\u041F\u043E\u0434\u043E\u0431\u0440\u0430\u043D\u043E 3 \u044F\u0431\u043B\u043E\u043A\u0430") == 0);
    ScopedEvent.Count = 5;
    FBY_CHECK(ScopedActions.Compose(ScopedEvent, ScopedLine, sizeof(ScopedLine)) > 0);
    FBY_CHECK(std::strcmp(ScopedLine, "\u041F\u043E\u0434\u043E\u0431\u0440\u0430\u043D\u043E 5 \u044F\u0431\u043B\u043E\u043A") == 0);
    FBY_CHECK(ScopedTable.SelectLocale("en"));

    // The narrator receives the composed line once and suppresses a repeat.
    Narrator ScopedNarrator;
    const TtsBackend ScopedBackend = MakeBackend();
    ScopedNarrator.SetBackend(&ScopedBackend);
    ScopedNarrator.SetOwner(NarratorOwner::Game);
    GSpeakCount = 0;
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 1020;
    ScopedEvent.Category = static_cast<std::uint8_t>(NarrationCategory::Interaction);
    ScopedEvent.DedupKey = 7;
    FBY_CHECK(ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    ScopedNarrator.Advance(0.0f);
    FBY_CHECK(GSpeakCount == 1);
    FBY_CHECK(std::strcmp(GLastSpoken, "Door opened") == 0);

    // A repeated key inside the window is filtered before composing.
    FBY_CHECK(!ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    ScopedActions.Advance(1.3f);
    ScopedNarrator.Advance(0.7f);
    FBY_CHECK(ScopedActions.Publish(ScopedEvent, ScopedNarrator));

    // Category mask and priority floor are independent gates.
    ActionNarrationSettings ScopedGated;
    ScopedGated.Categories = 1u << static_cast<std::uint8_t>(NarrationCategory::Interaction);
    ScopedGated.MinimumPriority = 2;
    ScopedActions.SetSettings(ScopedGated);
    ScopedEvent.Category = static_cast<std::uint8_t>(NarrationCategory::Combat);
    ScopedEvent.Priority = 3;
    FBY_CHECK(!ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    ScopedEvent.Category = static_cast<std::uint8_t>(NarrationCategory::Interaction);
    ScopedEvent.Priority = 1;
    FBY_CHECK(!ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    ScopedEvent.Priority = 2;
    ScopedEvent.DedupKey = 0;
    FBY_CHECK(ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    // An out-of-range category is rejected instead of wrapping a shift.
    ScopedEvent.Category = 200;
    FBY_CHECK(!ScopedActions.Publish(ScopedEvent, ScopedNarrator));
    ScopedActions.SetSettings(ActionNarrationSettings{});

    // Kernel: blind mode plus the world-event switch gate the whole layer.
    AccessibilityKernel ScopedKernel;
    ScopedKernel.SetTtsBackend(&ScopedBackend);
    ScopedKernel.SetBlindMode(true);
    ScopedKernel.SetActionLocalization(&ScopedTable);
    ScopedKernel.SetActionVocabulary(ScopedVocabulary);
    ScopedEvent = ActionEvent{};
    ScopedEvent.TemplateId = 1020;
    ScopedEvent.Category = static_cast<std::uint8_t>(NarrationCategory::Interaction);
    FBY_CHECK(ScopedKernel.PublishAction(ScopedEvent));
    FBY_CHECK(ScopedKernel.Speech().Queued() > 0);

    AccessibilityProfile ScopedProfile = ScopedKernel.Profile();
    ScopedProfile.narration.AnnounceWorldEvents = false;
    ScopedKernel.ApplyProfile(ScopedProfile);
    FBY_CHECK(!ScopedKernel.PublishAction(ScopedEvent));

    ScopedProfile.narration.AnnounceWorldEvents = true;
    ScopedProfile.runtime.flags &= ~AccessibilityBlindMode;
    ScopedKernel.ApplyProfile(ScopedProfile);
    FBY_CHECK(!ScopedKernel.PublishAction(ScopedEvent));

    ScopedProfile.runtime.flags |= AccessibilityBlindMode;
    ScopedProfile.actions.Categories = 0;
    ScopedKernel.ApplyProfile(ScopedProfile);
    FBY_CHECK(!ScopedKernel.PublishAction(ScopedEvent));
    ScopedKernel.StopBridge();

    if (GFailures == 0)
    {
        std::printf("action_narration_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "action_narration_test: %u failure(s)\n", GFailures);
    return 1;
}
