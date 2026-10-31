// FemboyEngine2D - accessibility as an engine-level policy, not an overlay.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/PostProcess.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/UI/NarrativeTree.hpp"
#include "Engine/UI/SoundCues.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

namespace Fby
{

class LocalizationTable;

class AudioSystem;
struct TtsBackend;
namespace Ui
{
struct WidgetCanvas;
}

// Colour vision
enum class ColorVisionDeficiency : std::uint8_t
{
    None = 0,
    Protanopia = 1,
    Deuteranopia = 2,
    Tritanopia = 3,
};

struct ColorMatrix3
{
    // Row-major 3x3. Linear-light sRGB in and out.
    float m[9]{
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
};

[[nodiscard]] ColorMatrix3 ColorVisionMatrix(ColorVisionDeficiency InDeficiency) noexcept;
// Blends towards the corrected image by `strength` (0 = untouched, 1 = full
[[nodiscard]] Color32 ApplyColorVision(Color32 InColor, const ColorMatrix3& InMatrix, float InStrength = 1.0f) noexcept;

// WCAG 2.2 relative luminance and contrast ratio. Use these when validating a
[[nodiscard]] float SrgbRelativeLuminance(Color32 InColor) noexcept;
[[nodiscard]] float ContrastRatio(Color32 InForeground, Color32 InBackground) noexcept;

// Flash guard (CPU mirror of display_safety.comp)
class FlashGuard
{
public:
    void Configure(float InThreshold = 0.20f, float InResidualGain = 0.35f) noexcept;
    // Returns the gain to apply to the current frame. `meanLuminance` is the
    [[nodiscard]] float Evaluate(float InMeanLuminance, float InDt) noexcept;
    void Reset() noexcept;
    [[nodiscard]] float CurrentGain() const noexcept
    {
        return MGain;
    }

private:
    float MThreshold = 0.20f;
    float MResidualGain = 0.85f;
    float MPreviousLuminance = 0.0f;
    float MGain = 1.0f;
    bool MPrimed = false;
};

// Narrative log (BlindMode output)
inline constexpr std::uint32_t kNarrativeCapacity = 128;
inline constexpr std::uint32_t kNarrativeTextBytes = 160;

struct NarrativeEvent
{
    std::uint64_t frame = 0;
    Entity entity = entt::null;
    Vector2D position{};
    std::uint32_t code = 0;
    std::uint32_t sequence = 0;
    std::uint16_t textLength = 0;
    std::uint8_t severity = 0;
    std::uint8_t reserved = 0;
    char text[kNarrativeTextBytes]{};
};

static_assert(std::is_trivially_copyable_v<NarrativeEvent>);

// SPSC ring. The simulation thread pushes, the speech thread pops. Each event
class NarrativeLog
{
public:
    static constexpr std::uint32_t kCapacity = kNarrativeCapacity;

    bool Push(std::uint32_t InCode, Entity InEntity, Vector2D InPosition, std::string_view InText,
              std::uint8_t InSeverity = 0) noexcept;
    bool Pop(NarrativeEvent& InEvent) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept;
    [[nodiscard]] std::uint64_t DroppedCount() const noexcept
    {
        return MDropped.load(std::memory_order_relaxed);
    }

private:
    std::array<NarrativeEvent, kCapacity> MEvents{};
    // Consumer
    alignas(64) std::atomic<std::uint32_t> MHead{0};
    // Producer
    alignas(64) std::atomic<std::uint32_t> MTail{0};
    std::atomic<std::uint64_t> MDropped{0};
    std::atomic<std::uint64_t> MSequence{0};
};

// Subtitles (DeafMode output)
inline constexpr std::uint32_t kSubtitleCapacity = 8;

struct SoundTrigger
{
    std::uint32_t cueId = 0;
    Entity source = entt::null;
    Vector2D position{};
    float duration = 3.0f;
    std::uint32_t priority = 0;
    // Reserved: speaker direction, icon id, ...
    std::uint32_t flags = 0;
};

struct SubtitleLine
{
    std::uint32_t cueId = 0;
    Entity source = entt::null;
    Vector2D position{};
    float remaining = 0.0f;
    std::uint32_t priority = 0;
    std::uint16_t textLength = 0;
    std::uint16_t reserved = 0;
    char text[kNarrativeTextBytes]{};
};

// One authored audio marker. `timeSeconds` is clip-local. The text is a fixed
struct TimedSoundCue
{
    float timeSeconds = 0.0f;
    SoundTrigger trigger{};
    std::uint16_t textLength = 0;
    std::uint16_t reserved = 0;
    char text[kNarrativeTextBytes]{};
};

static_assert(std::is_trivially_copyable_v<TimedSoundCue>);

class SubtitleQueue
{
public:
    static constexpr std::uint32_t kCapacity = kSubtitleCapacity;

    bool Publish(const SoundTrigger& InTrigger, std::string_view InText) noexcept;
    void Tick(float InDt) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] const SubtitleLine& At(std::uint32_t InIndex) const noexcept
    {
        return MLines[InIndex];
    }
    [[nodiscard]] std::uint64_t DroppedCount() const noexcept
    {
        return MDropped;
    }

private:
    std::array<SubtitleLine, kCapacity> MLines{};
    std::uint32_t MCount = 0;
    std::uint64_t MDropped = 0;
};

// Voice chat captions. The provider is polled once per frame; the game decides
// the text (a VAD speaker label or a speech-to-text backend) and the priority.
struct VoiceCaptionEvent
{
    std::uint8_t PlayerId = 0xFF;
    std::uint32_t CueId = 0;
    std::uint8_t Priority = 0;
    std::string_view Text;
};

struct VoiceCaptionProvider
{
    void* User = nullptr;
    bool (*Poll)(void* InUser, VoiceCaptionEvent* OutEvent) noexcept = nullptr;
};

// AccessibilityKernel
enum AccessibilityFlags : std::uint32_t
{
    AccessibilityDisabled = 0,
    AccessibilityBlindMode = 1u << 0,
    AccessibilityDeafMode = 1u << 1,
    AccessibilityFlashGuard = 1u << 2,
    AccessibilityColorVisionMode = 1u << 3,
};

struct AccessibilitySettings
{
    [[nodiscard]] friend bool operator==(const AccessibilitySettings&, const AccessibilitySettings&) = default;

    std::uint32_t flags = 0;
    ColorVisionDeficiency colorVision = ColorVisionDeficiency::None;
    float colorVisionStrength = 1.0f;
    float flashThreshold = 0.20f;
    float flashGain = 0.35f;
    // Subtitles without full DeafMode: the cue/voice queue is visible but the
    // Master bus keeps playing.
    bool showSubtitles = false;
};

// Presentation policy shared by the game and the editor. The same values drive
// the WidgetCanvas OLED orbit, the editor window shift and the motion budget.
struct AccessibilityPresentationSettings
{
    [[nodiscard]] friend bool operator==(const AccessibilityPresentationSettings&,
                                         const AccessibilityPresentationSettings&) = default;

    bool oledPixelShift = true;
    float oledIntervalSeconds = 60.0f;
    // 1 = full motion, 0 = static UI (caret blink, shakes, panel animation).
    float motionScale = 1.0f;
    // Static HUD elements fade after this idle time; 0 disables the fade.
    float hudFadeAfterIdleSeconds = 0.0f;
    // WCAG 2.2 minimum contrast a theme must meet; 1 disables the check.
    float minimumContrastRatio = 4.5f;
};

// Developer-side behaviour. The editor applies this section to its own UI; a
// shipped game never reads it.
struct AccessibilityEditorSettings
{
    [[nodiscard]] friend bool operator==(const AccessibilityEditorSettings&,
                                         const AccessibilityEditorSettings&) = default;

    bool blindMode = false;
    bool deafMode = false;
    bool flashGuard = true;
    bool highContrast = false;
    bool reduceMotion = false;
    // 0 none, 1 protanopia, 2 deuteranopia, 3 tritanopia
    std::uint8_t colorVision = 0;
    float colorVisionStrength = 1.0f;
    float flashThreshold = 0.20f;
};

// The one accessibility profile of the engine. The project file stores it, the
// editor edits it, the kernel applies it, and ApplyToFrame turns the runtime
// section into the frame policy. Trivially copyable, so it is saved, compared
// and passed by value like every other POD in the engine.
inline constexpr std::uint16_t kAccessibilityProfileVersion = 1;

struct AccessibilityProfile
{
    [[nodiscard]] friend bool operator==(const AccessibilityProfile&, const AccessibilityProfile&) = default;

    std::uint16_t version = kAccessibilityProfileVersion;
    AccessibilitySettings runtime{};
    AccessibilityPresentationSettings presentation{};
    AccessibilityEditorSettings editor{};
};

// 1.0 defaults: flash guard on, OLED orbit on, no colour correction, full
// motion. A project that never stored a profile gets exactly this.
[[nodiscard]] AccessibilityProfile DefaultAccessibilityProfile() noexcept;

// Flag helpers so storage, UI and the kernel agree on what a flag means.
[[nodiscard]] constexpr bool AccessibilityHasFlag(std::uint32_t InFlags, std::uint32_t InFlag) noexcept
{
    return (InFlags & InFlag) != 0;
}

constexpr void AccessibilitySetFlag(std::uint32_t* InFlags, std::uint32_t InFlag, bool InEnabled) noexcept
{
    if (InEnabled)
    {
        *InFlags |= InFlag;
    }
    else
    {
        *InFlags &= ~InFlag;
    }
}

class AccessibilityKernel
{
public:
    using SpeechSink = void (*)(void* InUser, const char* InText, std::size_t InLength);

    AccessibilityKernel() = default;

    void SetBlindMode(bool InEnabled) noexcept;
    [[nodiscard]] bool BlindMode() const noexcept;
    void SetDeafMode(bool InEnabled) noexcept;
    [[nodiscard]] bool DeafMode() const noexcept;
    // Subtitles are shown when DeafMode is on or this independent switch is set.
    void SetShowSubtitles(bool InEnabled) noexcept;
    [[nodiscard]] bool ShowSubtitles() const noexcept
    {
        return MSettings.showSubtitles;
    }
    [[nodiscard]] bool SubtitlesVisible() const noexcept
    {
        return DeafMode() || MSettings.showSubtitles;
    }
    void SetFlashGuard(bool InEnabled, float InThreshold = 0.20f, float InGain = 0.35f) noexcept;
    void SetColorVision(ColorVisionDeficiency InDeficiency, float InStrength = 1.0f) noexcept;

    [[nodiscard]] const AccessibilitySettings& Settings() const noexcept
    {
        return MSettings;
    }
    // Applies a whole profile in one call. This is the single entry point the
    // editor and the game share; the individual setters below stay for tests and
    // for code that changes one option at runtime.
    void ApplyProfile(const AccessibilityProfile& InProfile) noexcept;
    // Live view of the profile: the runtime section always reflects the settings
    // the individual setters may have changed since the last ApplyProfile().
    [[nodiscard]] AccessibilityProfile Profile() const noexcept
    {
        AccessibilityProfile ScopedCopy = MProfile;
        ScopedCopy.runtime = MSettings;
        return ScopedCopy;
    }
    [[nodiscard]] NarrativeLog& Log() noexcept
    {
        return MLog;
    }
    [[nodiscard]] SubtitleQueue& Subtitles() noexcept
    {
        return MSubtitles;
    }
    [[nodiscard]] FlashGuard& Guard() noexcept
    {
        return MGuard;
    }

    // Sets the presentation policy on a frame. This is the method the app calls
    void ApplyToFrame(Rhi::FramePacket& InPacket) noexcept;
    // Copies the colour-vision matrix request, the strength and the flash-guard
    // gain into a post-process policy. The composite pass then applies both terms,
    // so an app that runs PostFxPass gets the same protection as the editor.
    void ApplyPostFx(Rhi::PostFxSettings& InSettings) const noexcept;
    // CPU colour fallback for a backend or app that has no final composite pass:
    // corrects the clear colour and every item colour with the same matrix and
    // gain. Exact for the clear colour, approximate for alpha-blended sprites, and
    // it must not be combined with ApplyPostFx on the same frame.
    void ApplyCpuColorFallback(std::span<Rhi::RenderItem> InItems, Color32& InClear) const noexcept;

    // Fixed/PostUpdate call. In blind mode it emits physics contacts and
    void Update(Ecs::SystemContext& InContext) noexcept;

    // Sound timeline hook: call when an audio marker becomes audible. The
    void PublishSoundTrigger(const SoundTrigger& InTrigger, std::string_view InText) noexcept;

    // Cue-driven captions. The registry maps cueId -> locId, category, priority
    // and cooldown; the resolver turns a locId into localized text (normally
    // LocalizationTable::Find).
    void SetSoundCues(const SoundCueRegistry* InRegistry) noexcept;
    void SetCueTextResolver(std::string_view (*InResolver)(void*, std::uint32_t) noexcept, void* InUser) noexcept;
    // Adapter for the common case: cue text comes from the active locale with
    // fallback to the pack's default locale.
    void SetCueLocalization(const LocalizationTable* InTable) noexcept;
    void PublishCueSound(std::uint32_t InCueId, const SoundTrigger& InTrigger) noexcept;

    // Voice chat captions for DeafMode; BlindMode also mirrors them into the
    // narrative log.
    void SetVoiceCaptionProvider(const VoiceCaptionProvider* InProvider) noexcept;
    void AdvanceCaptionClock(float InDeltaSeconds) noexcept;
    void PollVoiceCaptions() noexcept;

    // Scans a clip timeline for cues crossed in (previousTime, currentTime] and
    void ProcessTimeline(std::span<const TimedSoundCue> InCues, float InPreviousTime, float InCurrentTime) noexcept;
    void PublishNarrative(std::uint32_t InCode, Entity InEntity, Vector2D InPosition, std::string_view InText,
                          std::uint8_t InSeverity = 0) noexcept;

    // Narrative Tree locale: "ru" selects Russian, everything else English.
    void SetNarrativeLocale(std::string_view InLocale) noexcept;
    // On-demand full scene narration (rebuilds the tree and publishes one
    void NarrateScene(Ecs::Registry& InRegistry) noexcept;

    void SetSpeechSink(SpeechSink InSink, void* InUser) noexcept;
    // Optional native backend (SAPI/AVSpeech). When set, FlushSpeech() routes
    void SetTtsBackend(const TtsBackend* InBackend) noexcept;
    // Main-thread only. The editor calls this from EditorSystem::Tick(); the
    void FlushSpeech() noexcept;

    // Builds the subtitle overlay into a dedicated canvas. Returns true when at
    bool BuildSubtitleCanvas(Ui::WidgetCanvas& InCanvas) const noexcept;

private:
    struct StateTrack
    {
        Entity entity = entt::null;
        GameplayStateMask bits = 0;
    };

    void TrackGameplayState(Ecs::Registry& InRegistry) noexcept;
    void EmitContacts(Ecs::SystemContext& InContext) noexcept;
    void ApplyTtsLanguage() noexcept;
    [[nodiscard]] StateTrack* FindTrack(Entity InEntity) noexcept;

    AccessibilitySettings MSettings{};
    AccessibilityProfile MProfile{};
    NarrativeLog MLog{};
    SubtitleQueue MSubtitles{};
    FlashGuard MGuard{};
    SpeechSink MSpeechSink = nullptr;
    void* MSpeechUser = nullptr;
    const TtsBackend* MTtsBackend = nullptr;
    char MTtsLanguage[16] = "en-US";
    std::thread::id MMainThread{};
    bool MMainThreadKnown = false;
    // Set when SetNarrativeLocale() changed the language off the main thread.
    bool MPendingVoiceLanguage = false;
    Ui::NarrativeTree MNarrative{};
    bool MNativeTtsAttached = false;
    bool MExternalTts = false;
    std::array<StateTrack, 64> MTracks{};
    std::uint32_t MTrackCount = 0;
    bool MAudioMuted = false;
    bool MAudioWasMuted = false;
    const SoundCueRegistry* MSoundCues = nullptr;
    std::string_view (*MCueTextResolver)(void*, std::uint32_t) noexcept = nullptr;
    void* MCueTextUser = nullptr;
    const VoiceCaptionProvider* MVoiceCaptionProvider = nullptr;
    float MCaptionClockMs = 0.0f;
};
}
