// FemboyEngine2D - data-pack registry for particle effect presets.
#pragma once

#include "Engine/Effects/ParticleEffects.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby
{

inline constexpr std::uint32_t kParticlePresetMaxCount = 128;
inline constexpr std::uint32_t kParticlePresetIdChars = 64;
inline constexpr std::uint32_t kParticlePresetNameChars = 64;
inline constexpr std::uint32_t kParticlePresetCategoryChars = 32;

enum ParticlePresetFlags : std::uint32_t
{
    ParticlePresetNone = 0,
    ParticlePresetBuiltIn = 1u << 0,
};

struct ParticlePresetDesc
{
    char Id[kParticlePresetIdChars]{};
    char DisplayName[kParticlePresetNameChars]{};
    char Category[kParticlePresetCategoryChars]{};
    ParticleEffectSettings Settings{};
    std::uint32_t Flags = ParticlePresetNone;
};

// Fixed-capacity registry shared by the built-ins, project data packs and any
// future plugin-owned presets. Values are copied; the registry never points
// into pack storage.
class ParticlePresetRegistry
{
public:
    [[nodiscard]] bool Add(const ParticlePresetDesc& InPreset, const void* InOwner = nullptr,
                           std::string* OutError = nullptr);
    void RemoveOwner(const void* InOwner) noexcept;
    void Clear() noexcept;
    // Rolls back presets added by a failed multi-entry load.
    void Truncate(std::uint32_t InCount) noexcept
    {
        if (InCount < MCount)
        {
            MCount = InCount;
        }
    }
    void RegisterBuiltIns();

    [[nodiscard]] const ParticlePresetDesc* Find(std::string_view InId) const noexcept;
    [[nodiscard]] const ParticlePresetDesc* At(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] std::span<const ParticlePresetDesc> Presets() const noexcept
    {
        return std::span<const ParticlePresetDesc>(MItems.data(), MCount);
    }

private:
    std::array<ParticlePresetDesc, kParticlePresetMaxCount> MItems{};
    std::array<const void*, kParticlePresetMaxCount> MOwners{};
    std::uint32_t MCount = 0;
};

// `.fbyparticle` files contain a `[[presets]]` array. Loaders validate every
// field, clamp the settings and report the preset id in the error string.
[[nodiscard]] bool LoadParticlePresetToml(std::string_view InText, ParticlePresetRegistry& InRegistry,
                                          const void* InOwner = nullptr, std::string* OutError = nullptr);
[[nodiscard]] bool LoadParticlePresetFile(std::string_view InPath, ParticlePresetRegistry& InRegistry,
                                          const void* InOwner = nullptr, std::string* OutError = nullptr);
// Writes one preset as a `[[presets]]` table. The editor uses it to save the
// current particle effect; the loader accepts exactly this shape.
[[nodiscard]] bool SaveParticlePresetToml(const ParticlePresetDesc& InPreset, std::string* OutText,
                                          std::string* OutError = nullptr);
[[nodiscard]] bool SaveParticlePresetFile(std::string_view InPath, const ParticlePresetDesc& InPreset,
                                          std::string* OutError = nullptr);
}
