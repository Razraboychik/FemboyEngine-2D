// FemboyEngine2D - impact, material and local damage infrastructure for SDF objects.
#pragma once

#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Physics/SdfCollision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kSdfMaxDamageParts = 32;
inline constexpr std::uint32_t kSdfMaxDamageConnections = 64;
inline constexpr std::uint32_t kSdfInvalidDamagePart = 0xFFFFFFFFu;
inline constexpr std::uint32_t kSdfPartBroken = 1u << 0;
inline constexpr std::uint32_t kSdfConnectionBroken = 1u << 0;

// Physical material values used by the default damage rule. The profile is data,
// not a behaviour: a game can replace the rule with its own callback.
struct SdfMaterialProfile
{
    // Resistance to damage; larger values make the same impact weaker.
    float Toughness = 1.0f;
    // Material hardness multiplier; larger values make the same impact stronger.
    float Hardness = 1.0f;
    // Impact energy below this threshold does no damage.
    float MinKineticEnergy = 0.0f;
    // Damage per unit of effective energy.
    float EnergyToDamage = 1.0f;
    // How strongly local thickness reduces the damage.
    float ThicknessExponent = 1.0f;
    // Upper bound on the integrity fraction removed by one impact.
    float MaximumDamageFraction = 1.0f;

    void Clamp() noexcept;
};

static_assert(std::is_trivially_copyable_v<SdfMaterialProfile>);

// One modular piece of a destructible object. The radius is a cheap point-hit
// proxy; a game can keep its real convex shape in its own fragment asset.
struct SdfPart
{
    Vector2D Center{};
    float Radius = 1.0f;
    float Integrity = 1.0f;
    float Mass = 1.0f;
    std::uint32_t Material = 0;
    std::uint32_t Flags = 0;

    [[nodiscard]] bool Broken() const noexcept
    {
        return (Flags & kSdfPartBroken) != 0;
    }
};

static_assert(std::is_trivially_copyable_v<SdfPart>);

// Structural link between two parts. A broken part breaks every link that
// touches it; a game may also break links from its own load model.
struct SdfConnection
{
    std::uint32_t PartA = kSdfInvalidDamagePart;
    std::uint32_t PartB = kSdfInvalidDamagePart;
    float Strength = 1.0f;
    float Load = 0.0f;
    std::uint32_t Flags = 0;

    [[nodiscard]] bool Broken() const noexcept
    {
        return (Flags & kSdfConnectionBroken) != 0;
    }
};

static_assert(std::is_trivially_copyable_v<SdfConnection>);

// One resolved impact. The contact fields are filled by SdfCollisionSystem before
// it corrects the body, so the energy is the approach energy rather than the
// push-out impulse.
struct SdfImpact
{
    Entity Target = entt::null;
    Vector2D Point{};
    Vector2D Normal{0.0f, -1.0f};
    float RelativeSpeed = 0.0f;
    float Impulse = 0.0f;
    float KineticEnergy = 0.0f;
    float Mass = 0.0f;
    // Local material depth along the inward normal. Zero means unknown.
    float LocalThickness = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SdfImpact>);

struct SdfImpactResult
{
    bool Applied = false;
    bool PartBroken = false;
    std::uint32_t BrokenConnections = 0;
    std::uint32_t PartIndex = kSdfInvalidDamagePart;
    float Damage = 0.0f;
    float RemainingIntegrity = 1.0f;
};

static_assert(std::is_trivially_copyable_v<SdfImpactResult>);

// Damage rule callback. Returning 0..1 removes that fraction of the part's
// integrity. The callback is optional; SdfDefaultDamage is the neutral default.
using SdfDamagePolicy = float (*)(void* InUser, const SdfImpact& InImpact, const SdfPart& InPart,
                                  const SdfMaterialProfile& InMaterial) noexcept;

// Neutral rule: (energy - minimum) * hardness / (toughness * thickness^exponent).
// It is intentionally simple; games are expected to replace it for armour,
// directional hits, weak points or scripted invulnerability.
[[nodiscard]] inline float SdfDefaultDamage(void*, const SdfImpact& InImpact, const SdfPart& InPart,
                                            const SdfMaterialProfile& InMaterial) noexcept
{
    (void)InPart;
    const float ScopedEnergy = std::max(InImpact.KineticEnergy - InMaterial.MinKineticEnergy, 0.0f);
    // Unknown thickness uses a neutral one-unit fallback instead of making the
    // hit arbitrarily destructive.
    const float ScopedThickness = InImpact.LocalThickness > 0.0f ? InImpact.LocalThickness : 1.0f;
    const float ScopedToughness = std::max(InMaterial.Toughness, 0.01f);
    const float ScopedHardness = std::max(InMaterial.Hardness, 0.01f);
    const float ScopedExponent = std::max(InMaterial.ThicknessExponent, 0.01f);
    const float ScopedDenominator = ScopedToughness * Sim::Pow(ScopedThickness, ScopedExponent);
    const float ScopedDamage = ScopedEnergy * InMaterial.EnergyToDamage * ScopedHardness / ScopedDenominator;
    return std::clamp(ScopedDamage, 0.0f, std::clamp(InMaterial.MaximumDamageFraction, 0.0f, 1.0f));
}

// Marches from a surface point into the solid to estimate local material depth.
// The normal points out of the solid. A point outside the field returns zero.
[[nodiscard]] inline float SdfThicknessInward(const SdfField& InField, Vector2D InPoint, Vector2D InNormal,
                                              float InMaxThickness = 32.0f) noexcept
{
    if (!InField.Valid() || InMaxThickness <= 0.0f)
    {
        return 0.0f;
    }
    const float ScopedNormalLength = std::sqrt(InNormal.LengthSquared());
    if (ScopedNormalLength < 1.0e-6f)
    {
        return 0.0f;
    }
    const Vector2D ScopedNormal = InNormal / ScopedNormalLength;
    if (InField.Sample(InPoint) > 0.0f)
    {
        return 0.0f;
    }
    const float ScopedStep = std::max(std::min(InField.cellSize.X, InField.cellSize.Y), 0.01f);
    for (float ScopedDistance = ScopedStep; ScopedDistance <= InMaxThickness; ScopedDistance += ScopedStep)
    {
        if (InField.Sample(InPoint - ScopedNormal * ScopedDistance) >= 0.0f)
        {
            return ScopedDistance;
        }
    }
    return InMaxThickness;
}

[[nodiscard]] inline SdfImpact MakeSdfImpact(const SdfContact& InContact, float InLocalThickness = 0.0f) noexcept
{
    SdfImpact ScopedImpact{};
    ScopedImpact.Target = InContact.entity;
    ScopedImpact.Point = InContact.point;
    ScopedImpact.Normal = InContact.normal;
    ScopedImpact.RelativeSpeed = InContact.RelativeSpeed;
    ScopedImpact.Impulse = InContact.impulse;
    ScopedImpact.KineticEnergy = InContact.KineticEnergy;
    ScopedImpact.Mass = InContact.Mass;
    ScopedImpact.LocalThickness = InLocalThickness;
    return ScopedImpact;
}

[[nodiscard]] inline SdfImpact MakeSdfImpact(const SdfContact& InContact, const SdfField& InField,
                                             float InMaxThickness = 32.0f) noexcept
{
    return MakeSdfImpact(InContact, SdfThicknessInward(InField, InContact.point, InContact.normal, InMaxThickness));
}

// Fixed-capacity damage and connection graph. The model is deterministic, has no
// RNG and never allocates; the game owns fracture assets, debris and visuals.
class SdfDamageModel
{
public:
    void Clear() noexcept;
    [[nodiscard]] bool AddPart(const SdfPart& InPart) noexcept;
    [[nodiscard]] bool AddConnection(const SdfConnection& InConnection) noexcept;

    [[nodiscard]] std::uint32_t PartCount() const noexcept
    {
        return MPartCount;
    }
    [[nodiscard]] std::uint32_t ConnectionCount() const noexcept
    {
        return MConnectionCount;
    }
    [[nodiscard]] std::span<const SdfPart> Parts() const noexcept
    {
        return {MParts.data(), MPartCount};
    }
    [[nodiscard]] std::span<const SdfConnection> Connections() const noexcept
    {
        return {MConnections.data(), MConnectionCount};
    }
    [[nodiscard]] SdfPart* Part(std::uint32_t InIndex) noexcept;
    [[nodiscard]] const SdfPart* Part(std::uint32_t InIndex) const noexcept;
    [[nodiscard]] SdfConnection* Connection(std::uint32_t InIndex) noexcept;
    [[nodiscard]] const SdfConnection* Connection(std::uint32_t InIndex) const noexcept;

    // Returns the nearest unbroken part whose radius contains the point, or
    // kSdfInvalidDamagePart when the point misses every part.
    [[nodiscard]] std::uint32_t FindPartAtPoint(Vector2D InPoint) const noexcept;

    // Applies one impact to a known part. A custom policy may replace the
    // default rule; passing null selects SdfDefaultDamage.
    [[nodiscard]] SdfImpactResult ApplyImpact(const SdfImpact& InImpact, std::uint32_t InPartIndex,
                                              const SdfMaterialProfile& InMaterial, SdfDamagePolicy InPolicy = nullptr,
                                              void* InPolicyUser = nullptr) noexcept;
    [[nodiscard]] SdfImpactResult ApplyImpactAtPoint(const SdfImpact& InImpact, const SdfMaterialProfile& InMaterial,
                                                     SdfDamagePolicy InPolicy = nullptr,
                                                     void* InPolicyUser = nullptr) noexcept;

    // Marks every connection that touches the part as broken and returns the
    // number that changed state.
    [[nodiscard]] std::uint32_t BreakConnectionsOf(std::uint32_t InPartIndex) noexcept;
    // Adds external structural load to one link. Returns true when the link
    // breaks, so a game can implement its own load propagation.
    [[nodiscard]] bool ApplyConnectionLoad(std::uint32_t InConnectionIndex, float InLoad) noexcept;

private:
    std::array<SdfPart, kSdfMaxDamageParts> MParts{};
    std::array<SdfConnection, kSdfMaxDamageConnections> MConnections{};
    std::uint32_t MPartCount = 0;
    std::uint32_t MConnectionCount = 0;
};

inline void SdfMaterialProfile::Clamp() noexcept
{
    Toughness = std::clamp(Toughness, 0.01f, 1000.0f);
    Hardness = std::clamp(Hardness, 0.01f, 1000.0f);
    MinKineticEnergy = std::clamp(MinKineticEnergy, 0.0f, 1.0e9f);
    EnergyToDamage = std::clamp(EnergyToDamage, 0.0f, 1000.0f);
    ThicknessExponent = std::clamp(ThicknessExponent, 0.01f, 8.0f);
    MaximumDamageFraction = std::clamp(MaximumDamageFraction, 0.0f, 1.0f);
}

inline void SdfDamageModel::Clear() noexcept
{
    MParts.fill(SdfPart{});
    MConnections.fill(SdfConnection{});
    MPartCount = 0;
    MConnectionCount = 0;
}

inline bool SdfDamageModel::AddPart(const SdfPart& InPart) noexcept
{
    if (MPartCount >= kSdfMaxDamageParts)
    {
        return false;
    }
    SdfPart ScopedPart = InPart;
    ScopedPart.Radius = std::max(ScopedPart.Radius, 0.01f);
    ScopedPart.Integrity = std::clamp(ScopedPart.Integrity, 0.0f, 1.0f);
    ScopedPart.Mass = std::max(ScopedPart.Mass, 0.001f);
    if (ScopedPart.Integrity <= 0.0f)
    {
        ScopedPart.Flags |= kSdfPartBroken;
    }
    MParts[MPartCount] = ScopedPart;
    ++MPartCount;
    return true;
}

inline bool SdfDamageModel::AddConnection(const SdfConnection& InConnection) noexcept
{
    if (MConnectionCount >= kSdfMaxDamageConnections || InConnection.PartA >= MPartCount ||
        InConnection.PartB >= MPartCount || InConnection.PartA == InConnection.PartB)
    {
        return false;
    }
    SdfConnection ScopedConnection = InConnection;
    ScopedConnection.Strength = std::max(ScopedConnection.Strength, 0.0f);
    MConnections[MConnectionCount] = ScopedConnection;
    ++MConnectionCount;
    return true;
}

inline SdfPart* SdfDamageModel::Part(std::uint32_t InIndex) noexcept
{
    return InIndex < MPartCount ? &MParts[InIndex] : nullptr;
}

inline const SdfPart* SdfDamageModel::Part(std::uint32_t InIndex) const noexcept
{
    return InIndex < MPartCount ? &MParts[InIndex] : nullptr;
}

inline SdfConnection* SdfDamageModel::Connection(std::uint32_t InIndex) noexcept
{
    return InIndex < MConnectionCount ? &MConnections[InIndex] : nullptr;
}

inline const SdfConnection* SdfDamageModel::Connection(std::uint32_t InIndex) const noexcept
{
    return InIndex < MConnectionCount ? &MConnections[InIndex] : nullptr;
}

inline std::uint32_t SdfDamageModel::FindPartAtPoint(Vector2D InPoint) const noexcept
{
    std::uint32_t ScopedBest = kSdfInvalidDamagePart;
    float ScopedBestDistance = 1.0e30f;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MPartCount; ++ScopedIndex)
    {
        const SdfPart& ScopedPart = MParts[ScopedIndex];
        if (ScopedPart.Broken())
        {
            continue;
        }
        const float ScopedDistance = (InPoint - ScopedPart.Center).LengthSquared();
        if (ScopedDistance <= ScopedPart.Radius * ScopedPart.Radius && ScopedDistance < ScopedBestDistance)
        {
            ScopedBest = ScopedIndex;
            ScopedBestDistance = ScopedDistance;
        }
    }
    return ScopedBest;
}

inline SdfImpactResult SdfDamageModel::ApplyImpact(const SdfImpact& InImpact, std::uint32_t InPartIndex,
                                                   const SdfMaterialProfile& InMaterial, SdfDamagePolicy InPolicy,
                                                   void* InPolicyUser) noexcept
{
    SdfImpactResult ScopedResult{};
    SdfPart* ScopedPart = Part(InPartIndex);
    if (ScopedPart == nullptr || ScopedPart->Broken())
    {
        return ScopedResult;
    }
    SdfMaterialProfile ScopedMaterial = InMaterial;
    ScopedMaterial.Clamp();
    const float ScopedRawDamage = InPolicy != nullptr
                                      ? InPolicy(InPolicyUser, InImpact, *ScopedPart, ScopedMaterial)
                                      : SdfDefaultDamage(InPolicyUser, InImpact, *ScopedPart, ScopedMaterial);
    const float ScopedDamage = std::isfinite(ScopedRawDamage) ? std::clamp(ScopedRawDamage, 0.0f, 1.0f) : 0.0f;
    ScopedResult.Applied = true;
    ScopedResult.PartIndex = InPartIndex;
    ScopedResult.Damage = ScopedDamage;
    ScopedPart->Integrity = std::max(ScopedPart->Integrity - ScopedDamage, 0.0f);
    ScopedResult.RemainingIntegrity = ScopedPart->Integrity;
    if (ScopedPart->Integrity <= 0.0f && !ScopedPart->Broken())
    {
        ScopedPart->Flags |= kSdfPartBroken;
        ScopedResult.PartBroken = true;
        ScopedResult.BrokenConnections = BreakConnectionsOf(InPartIndex);
    }
    return ScopedResult;
}

inline SdfImpactResult SdfDamageModel::ApplyImpactAtPoint(const SdfImpact& InImpact,
                                                          const SdfMaterialProfile& InMaterial,
                                                          SdfDamagePolicy InPolicy, void* InPolicyUser) noexcept
{
    return ApplyImpact(InImpact, FindPartAtPoint(InImpact.Point), InMaterial, InPolicy, InPolicyUser);
}

inline bool SdfDamageModel::ApplyConnectionLoad(std::uint32_t InConnectionIndex, float InLoad) noexcept
{
    SdfConnection* ScopedConnection = Connection(InConnectionIndex);
    if (ScopedConnection == nullptr || ScopedConnection->Broken())
    {
        return false;
    }
    ScopedConnection->Load = std::max(ScopedConnection->Load + std::max(InLoad, 0.0f), 0.0f);
    if (ScopedConnection->Load >= ScopedConnection->Strength)
    {
        ScopedConnection->Flags |= kSdfConnectionBroken;
        return true;
    }
    return false;
}

inline std::uint32_t SdfDamageModel::BreakConnectionsOf(std::uint32_t InPartIndex) noexcept
{
    if (InPartIndex >= MPartCount)
    {
        return 0;
    }
    std::uint32_t ScopedBroken = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MConnectionCount; ++ScopedIndex)
    {
        SdfConnection& ScopedConnection = MConnections[ScopedIndex];
        if (ScopedConnection.Broken())
        {
            continue;
        }
        if (ScopedConnection.PartA == InPartIndex || ScopedConnection.PartB == InPartIndex)
        {
            ScopedConnection.Flags |= kSdfConnectionBroken;
            ++ScopedBroken;
        }
    }
    return ScopedBroken;
}
}
