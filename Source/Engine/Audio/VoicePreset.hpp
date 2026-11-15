// FemboyEngine2D - voice effect preset registry and data-pack format.
#pragma once

#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceEffects.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fby
{

class VoiceSession;
class VoiceSystem;

inline constexpr std::uint32_t kVoicePresetMaxCount = 64;
inline constexpr std::uint32_t kVoicePresetIdBytes = 64;
inline constexpr std::uint32_t kVoicePresetNameKeyBytes = 96;

enum class VoicePresetCategory : std::uint8_t
{
    None = 0,
    Environment,
    Moderation,
    Cosmetic,
    Character,
    Scene,
    Count,
};

// A preset is data: the registry owns the values and every consumer copies a
// VoiceEffectProfile, so unloading the source never leaves a dangling profile.
enum VoicePresetFlags : std::uint32_t
{
    VoicePresetFlagNone = 0,
    VoicePresetFlagCosmetic = 1u << 0,
    VoicePresetFlagModeration = 1u << 1,
    VoicePresetFlagAllowUplink = 1u << 2,
    VoicePresetFlagAllowDownlink = 1u << 3,
};

[[nodiscard]] const char* VoicePresetCategoryName(VoicePresetCategory InCategory) noexcept;
[[nodiscard]] VoicePresetCategory VoicePresetCategoryFromName(std::string_view InName) noexcept;

struct VoicePresetDesc
{
    char Id[kVoicePresetIdBytes]{};
    char NameKey[kVoicePresetNameKeyBytes]{};
    VoicePresetCategory Category = VoicePresetCategory::None;
    std::uint32_t Flags = VoicePresetFlagNone;
    float MinIntensity = 0.0f;
    float MaxIntensity = 1.0f;
    VoiceEffectProfile Profile{};
};

class VoicePresetRegistry
{
public:
    VoicePresetRegistry() = default;

    // Copies the preset. Duplicate ids, a bad id, an unknown category and a
    // profile that cannot be clamped into range are rejected with a message.
    [[nodiscard]] bool Add(const VoicePresetDesc& InPreset, const void* InOwner = nullptr,
                           std::string* OutError = nullptr);
    void RemoveOwner(const void* InOwner) noexcept;
    void Clear() noexcept;
    void RegisterBuiltIns();

    [[nodiscard]] const VoicePresetDesc* Find(std::string_view InId) const noexcept;
    [[nodiscard]] const VoicePresetDesc* At(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCount == 0;
    }

private:
    struct Entry
    {
        VoicePresetDesc Desc{};
        const void* Owner = nullptr;
    };

    std::array<Entry, kVoicePresetMaxCount> MEntries{};
    std::uint32_t MCount = 0;
};

// One listener-side choice. The id is data, so it survives a registry reload
// and can be persisted outside the engine.
struct VoicePresetChoice
{
    char Id[kVoicePresetIdBytes]{};
    float Intensity = 1.0f;
    bool Enabled = false;

    void Set(std::string_view InId, float InIntensity = 1.0f) noexcept;
    void Clear() noexcept;
};

struct VoicePresetSelection
{
    VoicePresetChoice Local{};
    std::array<VoicePresetChoice, kVoiceMaxPeers> Participants{};

    void Clear() noexcept;
};

// Policy belongs to the session or the game, never to the speaker: a listener
// decides what a moderated participant sounds like. The ids name presets so a
// data pack can replace the built-in choices without recompiling.
struct VoiceEffectPolicy
{
    char ModerationPresetId[kVoicePresetIdBytes] = "Fby.Voice.ModerationMumble";
    char HardMutePresetId[kVoicePresetIdBytes] = "Fby.Voice.Muted";
    bool MumbleWhenModerated = true;
    bool AllowLocalUplink = true;
};

struct VoicePresetResolution
{
    const VoicePresetDesc* Preset = nullptr;
    float Intensity = 1.0f;
    bool Moderation = false;
};

// Resolves the choice for one participant. A locally muted participant uses the
// moderation preset even when the selection names a cosmetic one; a disabled or
// missing preset resolves to null, which clears the rack.
[[nodiscard]] VoicePresetResolution ResolveVoicePreset(const VoicePresetRegistry& InPresets,
                                                       const VoiceEffectPolicy& InPolicy, const VoiceSession& InSession,
                                                       std::uint8_t InPlayer,
                                                       const VoicePresetChoice& InChoice) noexcept;

// Scales every slot mix by the resolved intensity and returns a value the rack
// can copy. The preset itself is never referenced by the rack.
[[nodiscard]] VoiceEffectProfile BuildVoicePresetProfile(const VoicePresetResolution& InResolution) noexcept;

// Applies the current selection to a VoiceSystem. Configure() resets the rack
// filters, so the applier only calls the system when the resolved value
// actually changed.
class VoicePresetApplier
{
public:
    void Apply(VoiceSystem& InVoice, const VoiceSession& InSession, const VoicePresetRegistry& InPresets,
               const VoiceEffectPolicy& InPolicy, const VoicePresetSelection& InSelection) noexcept;
    void Reset() noexcept;

private:
    struct Applied
    {
        char Id[kVoicePresetIdBytes]{};
        float Intensity = 0.0f;
        bool Moderation = false;
        bool Valid = false;
    };

    std::array<Applied, kVoiceMaxPeers> MParticipants{};
    Applied MLocal{};
};

[[nodiscard]] bool LoadVoicePresetToml(std::string_view InText, VoicePresetRegistry& InRegistry,
                                       const void* InOwner = nullptr, std::string* OutError = nullptr);
[[nodiscard]] bool LoadVoicePresetFile(std::string_view InPath, VoicePresetRegistry& InRegistry,
                                       const void* InOwner = nullptr, std::string* OutError = nullptr);
}
