// FemboyEngine2D - semantic tree, projection, narrator and bridge policy tests.
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/AccessibilityBridge.hpp"
#include "Engine/UI/AccessibilityProjection.hpp"
#include "Engine/UI/AccessibilityTree.hpp"
#include "Engine/UI/Narrator.hpp"
#include "Engine/UI/PinkUI.hpp"
#include "Engine/UI/Tts.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
std::atomic<std::uint32_t> GSpeakCount{0};
std::atomic<std::uint32_t> GStopCount{0};
char GLastSpoken[512]{};
char GLastVoice[32]{};

bool TestSpeak(void*, const char* InText, std::size_t InLength, Fby::TtsPriority) noexcept
{
    GSpeakCount.fetch_add(1, std::memory_order_relaxed);
    const std::size_t ScopedLength = InLength < sizeof(GLastSpoken) - 1 ? InLength : sizeof(GLastSpoken) - 1;
    std::memcpy(GLastSpoken, InText, ScopedLength);
    GLastSpoken[ScopedLength] = '\0';
    return true;
}

void TestStop(void*) noexcept
{
    GStopCount.fetch_add(1, std::memory_order_relaxed);
}

char GLastVoiceId[192]{};
bool GRejectVoiceId = false;

bool TestSetVoiceById(void*, const char* InId) noexcept
{
    if (GRejectVoiceId || InId == nullptr)
    {
        return false;
    }
    std::snprintf(GLastVoiceId, sizeof(GLastVoiceId), "%s", InId);
    return true;
}

std::uint32_t TestEnumerateVoices(void*, Fby::TtsVoiceInfo* OutVoices, std::uint32_t InCapacity) noexcept
{
    if (OutVoices == nullptr || InCapacity == 0)
    {
        return 0;
    }
    std::snprintf(OutVoices[0].Id, sizeof(OutVoices[0].Id), "voice.one");
    std::snprintf(OutVoices[0].Name, sizeof(OutVoices[0].Name), "Test Voice One");
    std::snprintf(OutVoices[0].Language, sizeof(OutVoices[0].Language), "en-GB");
    OutVoices[0].Gender = 2;
    if (InCapacity > 1)
    {
        std::snprintf(OutVoices[1].Id, sizeof(OutVoices[1].Id), "voice.two");
        std::snprintf(OutVoices[1].Name, sizeof(OutVoices[1].Name), "Test Voice Two");
        std::snprintf(OutVoices[1].Language, sizeof(OutVoices[1].Language), "de-DE");
        OutVoices[1].Gender = 1;
        return 2;
    }
    return 1;
}

void TestSetVoice(void*, const char* InLanguage) noexcept
{
    if (InLanguage == nullptr)
    {
        GLastVoiceId[0] = '\0';
        GLastVoice[0] = '\0';
        return;
    }
    std::snprintf(GLastVoice, sizeof(GLastVoice), "%s", InLanguage);
}

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
}

int main()
{
    // Defaults: speech and the bridge are on out of the box, English voice.
    const Fby::AccessibilityProfile ScopedDefaults = Fby::DefaultAccessibilityProfile();
    FBY_CHECK(ScopedDefaults.narration.Enabled);
    FBY_CHECK(ScopedDefaults.bridge.Enabled);
    FBY_CHECK(std::strcmp(ScopedDefaults.narration.Language, "en-US") == 0);
    FBY_CHECK(ScopedDefaults.narration.AnnounceFocus);
    FBY_CHECK(ScopedDefaults.narration.MaxQueued == 8);

    // Tree: structure, focus and the resolved name.
    Fby::Ui::AccessibilityTree ScopedTree;
    const std::uint32_t ScopedRoot = ScopedTree.AddNode(
        1u, Fby::Ui::AccessibilityRole::Window, Fby::Ui::kNoAccessibilityNode,
        Fby::Vector4D{0.0f, 0.0f, 1920.0f, 1080.0f}, Fby::Ui::AccessibilityStateEnabled, 0, "Main menu");
    const std::uint32_t ScopedButton = ScopedTree.AddNode(
        2u, Fby::Ui::AccessibilityRole::Button, ScopedRoot, Fby::Vector4D{10.0f, 20.0f, 210.0f, 60.0f},
        Fby::Ui::AccessibilityStateEnabled | Fby::Ui::AccessibilityStateFocused, 0, "Play");
    FBY_CHECK(ScopedRoot != Fby::Ui::kNoAccessibilityNode);
    FBY_CHECK(ScopedButton != Fby::Ui::kNoAccessibilityNode);
    FBY_CHECK(ScopedTree.Count() == 2);
    FBY_CHECK(ScopedTree.FirstChildOf(ScopedRoot) == ScopedButton);
    FBY_CHECK(ScopedTree.At(ScopedButton).Role == Fby::Ui::AccessibilityRole::Button);
    FBY_CHECK(ScopedTree.At(ScopedButton).Name == "Play");
    FBY_CHECK(std::strcmp(Fby::Ui::AccessibilityRoleName(Fby::Ui::AccessibilityRole::Button), "button") == 0);

    ScopedTree.SetFocusedId(2u);
    FBY_CHECK(ScopedTree.FocusedId() == 2u);
    FBY_CHECK(ScopedTree.FocusedIndex() == ScopedButton);
    Fby::Ui::AccessibilityEvent ScopedEvent{};
    bool ScopedSawFocus = false;
    while (ScopedTree.PollEvent(ScopedEvent))
    {
        if (ScopedEvent.Kind == Fby::Ui::AccessibilityEventKind::Focus && ScopedEvent.Id == 2u)
        {
            ScopedSawFocus = true;
        }
    }
    FBY_CHECK(ScopedSawFocus);

    // A rebuild that repeats the same ids is silent; a different structure is not.
    ScopedTree.Reset();
    (void)ScopedTree.AddNode(1u, Fby::Ui::AccessibilityRole::Window, Fby::Ui::kNoAccessibilityNode, Fby::Vector4D{},
                             Fby::Ui::AccessibilityStateEnabled, 0, "Main menu");
    bool ScopedSawStructure = false;
    while (ScopedTree.PollEvent(ScopedEvent))
    {
        ScopedSawStructure |= ScopedEvent.Kind == Fby::Ui::AccessibilityEventKind::Structure;
    }
    FBY_CHECK(ScopedSawStructure);
    ScopedTree.Reset();
    (void)ScopedTree.AddNode(1u, Fby::Ui::AccessibilityRole::Window, Fby::Ui::kNoAccessibilityNode, Fby::Vector4D{},
                             Fby::Ui::AccessibilityStateEnabled, 0, "Main menu");
    // One full rebuild establishes the baseline hash, the next identical one
    // must stay silent.
    ScopedTree.Reset();
    (void)ScopedTree.AddNode(1u, Fby::Ui::AccessibilityRole::Window, Fby::Ui::kNoAccessibilityNode, Fby::Vector4D{},
                             Fby::Ui::AccessibilityStateEnabled, 0, "Main menu");
    while (ScopedTree.PollEvent(ScopedEvent))
    {
    }
    ScopedTree.Reset();
    (void)ScopedTree.AddNode(1u, Fby::Ui::AccessibilityRole::Window, Fby::Ui::kNoAccessibilityNode, Fby::Vector4D{},
                             Fby::Ui::AccessibilityStateEnabled, 0, "Main menu");
    ScopedSawStructure = false;
    while (ScopedTree.PollEvent(ScopedEvent))
    {
        ScopedSawStructure |= ScopedEvent.Kind == Fby::Ui::AccessibilityEventKind::Structure;
    }
    FBY_CHECK(!ScopedSawStructure);

    // Projection: a canvas with a focused button becomes roles, parents and focus.
    Fby::Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.SetCanvasSize(Fby::Vector2D{1280.0f, 720.0f});
    Fby::Ui::WidgetDesc ScopedPanelDesc{};
    ScopedPanelDesc.kind = Fby::Ui::WidgetKind::Panel;
    const std::int32_t ScopedPanel = ScopedCanvas.AddWidget(ScopedPanelDesc);
    Fby::Ui::WidgetDesc ScopedButtonDesc{};
    ScopedButtonDesc.kind = Fby::Ui::WidgetKind::Button;
    ScopedButtonDesc.parent = ScopedPanel;
    ScopedButtonDesc.text = "Start";
    ScopedButtonDesc.focusPolicy = Fby::Ui::FocusPolicy::TabAndPointer;
    ScopedButtonDesc.offsetMin = Fby::Vector2D{40.0f, 40.0f};
    ScopedButtonDesc.offsetMax = Fby::Vector2D{240.0f, 100.0f};
    const std::int32_t ScopedStart = ScopedCanvas.AddWidget(ScopedButtonDesc);
    FBY_CHECK(ScopedPanel >= 0);
    FBY_CHECK(ScopedStart >= 0);
    Fby::Ui::SetFocus(ScopedCanvas, ScopedStart);
    FBY_CHECK(Fby::Ui::FocusedWidget(ScopedCanvas) == ScopedStart);

    Fby::Ui::AccessibilityTree ScopedProjected;
    Fby::Ui::AccessibilityProjectionStats ScopedStats{};
    const std::uint32_t ScopedProjectedCount =
        Fby::Ui::ProjectWidgetCanvas(ScopedCanvas, ScopedProjected, &ScopedStats);
    FBY_CHECK(ScopedProjectedCount == 2);
    FBY_CHECK(ScopedStats.Nodes == 2);
    FBY_CHECK(ScopedStats.SkippedInvisible == 0);
    FBY_CHECK(ScopedProjected.FocusedId() != 0);
    const std::uint32_t ScopedFocusedIndex = ScopedProjected.FocusedIndex();
    FBY_CHECK(ScopedFocusedIndex != Fby::Ui::kNoAccessibilityNode);
    const Fby::Ui::AccessibilityNodeView ScopedFocusedNode = ScopedProjected.At(ScopedFocusedIndex);
    FBY_CHECK(ScopedFocusedNode.Role == Fby::Ui::AccessibilityRole::Button);
    FBY_CHECK(ScopedFocusedNode.Name == "Start");
    FBY_CHECK((ScopedFocusedNode.States & Fby::Ui::AccessibilityStateFocused) != 0);
    FBY_CHECK(ScopedFocusedNode.ParentIndex != Fby::Ui::kNoAccessibilityNode);
    FBY_CHECK(std::strcmp(Fby::Ui::AccessibilityRoleName(ScopedProjected.At(0).Role), "pane") == 0);

    // Narrator: settings, chunking, dedup, priority and owner arbitration.
    Fby::TtsBackend ScopedBackend;
    ScopedBackend.speak = &TestSpeak;
    ScopedBackend.stop = &TestStop;
    ScopedBackend.setVoice = &TestSetVoice;
    ScopedBackend.setVoiceById = &TestSetVoiceById;
    ScopedBackend.enumerateVoices = &TestEnumerateVoices;

    Fby::Narrator ScopedNarrator;
    Fby::NarratorSettings ScopedSettings;
    ScopedSettings.MaxQueued = 2;
    ScopedNarrator.SetSettings(ScopedSettings);
    ScopedNarrator.SetBackend(&ScopedBackend);
    ScopedNarrator.SetOwner(Fby::NarratorOwner::Editor);
    FBY_CHECK(!ScopedNarrator.Enqueue(Fby::NarratorOwner::Game, Fby::NarratorPriority::Normal, "wrong owner"));

    GSpeakCount.store(0);
    FBY_CHECK(ScopedNarrator.Enqueue(Fby::NarratorOwner::Editor, Fby::NarratorPriority::Normal, "hello", 7u));
    // Same key inside the dedup window is dropped.
    FBY_CHECK(!ScopedNarrator.Enqueue(Fby::NarratorOwner::Editor, Fby::NarratorPriority::Normal, "hello", 7u));
    ScopedNarrator.Advance(0.01f);
    FBY_CHECK(GSpeakCount.load() == 1);
    FBY_CHECK(std::strcmp(GLastSpoken, "hello") == 0);
    FBY_CHECK(ScopedNarrator.Speaking());

    // The speaking estimate keeps the queue from stacking; advance past it.
    ScopedNarrator.Advance(2.0f);
    FBY_CHECK(!ScopedNarrator.Speaking());

    // Chunking: 400 bytes become two utterances, the queue keeps the first.
    std::string ScopedLong(400, 'a');
    ScopedNarrator.Clear();
    FBY_CHECK(ScopedNarrator.Enqueue(Fby::NarratorOwner::Editor, Fby::NarratorPriority::Normal, ScopedLong));
    FBY_CHECK(ScopedNarrator.Queued() == 2);

    // Priority replacement when the queue is full: a critical line evicts a low one.
    ScopedNarrator.Clear();
    ScopedSettings.MaxQueued = 1;
    ScopedNarrator.SetSettings(ScopedSettings);
    FBY_CHECK(ScopedNarrator.Enqueue(Fby::NarratorOwner::Editor, Fby::NarratorPriority::Low, "low one"));
    FBY_CHECK(ScopedNarrator.Enqueue(Fby::NarratorOwner::Editor, Fby::NarratorPriority::Critical, "urgent"));
    FBY_CHECK(ScopedNarrator.Queued() == 1);

    // Language: the narrator resolves the best installed voice and pins it by
    // id, so ja-JP with only en/de installed falls back to the English voice.
    GLastVoiceId[0] = '\0';
    GLastVoice[0] = '\0';
    ScopedNarrator.SetLanguage("ja-JP");
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice.one") == 0);
    ScopedNarrator.SetLanguage("pt_BR");
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice.one") == 0);

    // An explicit voice id wins over the language; a rejected id falls back.
    Fby::NarratorSettings ScopedVoiceSettings = ScopedNarrator.Settings();
    std::snprintf(ScopedVoiceSettings.VoiceId, sizeof(ScopedVoiceSettings.VoiceId), "voice.one");
    ScopedNarrator.SetSettings(ScopedVoiceSettings);
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice.one") == 0);

    GRejectVoiceId = true;
    GLastVoiceId[0] = '\0';
    GLastVoice[0] = '\0';
    std::snprintf(ScopedVoiceSettings.VoiceId, sizeof(ScopedVoiceSettings.VoiceId), "voice.missing");
    std::snprintf(ScopedVoiceSettings.Language, sizeof(ScopedVoiceSettings.Language), "de-DE");
    ScopedNarrator.SetSettings(ScopedVoiceSettings);
    FBY_CHECK(std::strcmp(GLastVoice, "de-DE") == 0);
    GRejectVoiceId = false;

    // Enumeration reaches the backend with the requested capacity.
    std::array<Fby::TtsVoiceInfo, 4> ScopedVoices{};
    FBY_CHECK(ScopedNarrator.Backend()->enumerateVoices(ScopedNarrator.Backend()->user, ScopedVoices.data(), 4) == 2);
    FBY_CHECK(std::strcmp(ScopedVoices[0].Id, "voice.one") == 0);
    FBY_CHECK(std::strcmp(ScopedVoices[1].Language, "de-DE") == 0);

    // Kernel: a focused canvas node is spoken once through the installed backend.
    GSpeakCount.store(0);
    GLastSpoken[0] = '\0';
    Fby::AccessibilityKernel ScopedKernel;
    ScopedKernel.SetTtsBackend(&ScopedBackend);
    ScopedKernel.SetBlindMode(true);
    ScopedKernel.SetNarrativeLocale("ru-RU");
    FBY_CHECK(std::strcmp(GLastVoiceId, "voice.one") == 0);
    ScopedKernel.ProjectCanvas(ScopedCanvas);
    FBY_CHECK(ScopedKernel.Tree().Count() == 2);
    ScopedKernel.FlushSpeech();
    FBY_CHECK(GSpeakCount.load() == 1);
    FBY_CHECK(std::strstr(GLastSpoken, "Start") != nullptr);
    // Focus narration is verbose: the role follows the name.
    FBY_CHECK(std::strstr(GLastSpoken, "button") != nullptr);

    // The bridge is off on a platform without a provider and never blocks.
    FBY_CHECK(!Fby::AccessibilityBridgeRunning());
    Fby::AccessibilityBridgeSettings ScopedBridge;
    ScopedBridge.Enabled = false;
    FBY_CHECK(!Fby::AccessibilityKernel().StartBridge(nullptr));

    // The native backend reports installed voices; a headless machine may
    // report none, which is a valid state and must not fail the test.
    std::array<Fby::TtsVoiceInfo, 8> ScopedInstalled{};
    const std::uint32_t ScopedInstalledCount =
        Fby::EnumerateNativeTtsVoices(ScopedInstalled.data(), static_cast<std::uint32_t>(ScopedInstalled.size()));
    std::printf("native TTS voices: %u\n", static_cast<unsigned>(ScopedInstalledCount));
    FBY_CHECK(ScopedInstalledCount <= ScopedInstalled.size());
    if (ScopedInstalledCount > 0)
    {
        FBY_CHECK(ScopedInstalled[0].Id[0] != '\0');
    }

    ScopedKernel.SetBlindMode(false);
    FBY_CHECK(GStopCount.load() >= 1);

    if (GFailures == 0)
    {
        std::printf("accessibility_narrator_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "accessibility_narrator_test: %u failure(s)\n", GFailures);
    return 1;
}
