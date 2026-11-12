// FemboyEngine2D - hybrid 2D SDF collision for Box2D v3 bodies.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/Physics/Physics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mdspan>
#include <span>
#include <type_traits>

namespace Fby
{

// A non-owning view of a 2D distance field. Negative values are solid. The grid
struct SdfField
{
    using Extents = std::dextents<std::size_t, 2>;
    using View = std::mdspan<const float, Extents>;

    const float* samples = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Vector2D origin{};
    Vector2D cellSize{1.0f, 1.0f};
    // Distance reported outside the grid; keep it large and positive
    float outsideDistance = 1.0e6f;
    // Bumped by the owner whenever the samples are edited in place; the GPU
    // upload cache keys on it so an in-place change is not ignored.
    std::uint32_t version = 0;

    [[nodiscard]] bool Valid() const noexcept
    {
        // A zero or negative cell size would divide by zero in Sample().
        return samples != nullptr && width > 1 && height > 1 && cellSize.X > 0.0f && cellSize.Y > 0.0f;
    }
    [[nodiscard]] View View2D() const noexcept
    {
        return View(samples, static_cast<std::size_t>(height), static_cast<std::size_t>(width));
    }
    [[nodiscard]] float At(std::uint32_t InX, std::uint32_t InY) const noexcept;
    [[nodiscard]] float Sample(Vector2D InWorld) const noexcept;
    [[nodiscard]] Vector2D Gradient(Vector2D InWorld) const noexcept;
};

struct SdfCollisionDesc
{
    // Positional correction fraction per fixed step, 0..1
    float pushOut = 0.65f;
    // Fraction of the inward normal velocity removed on contact
    float restitution = 0.05f;
    // Upper bound on one body's impulse, pixels * mass / second
    float maxImpulse = 4000.0f;
    // Upper bound on the depenetration speed, pixels / second. The correction is
    // applied as a velocity, never as a transform teleport.
    float maxPushSpeed = 2000.0f;
    // Extra samples per axis; 0 and 1 both produce the 2x2 corner grid.
    std::uint32_t subSamples = 1;
    // Skip sensors and static/kinematic bodies
    bool dynamicOnly = true;
};

struct SdfContact
{
    Entity EntityHandle = Fby::kNullEntity;
    Vector2D Point{};
    Vector2D Normal{0.0f, -1.0f};
    float Penetration = 0.0f;
    float Impulse = 0.0f;
    // Pre-correction approach speed along the contact normal. Damage systems use
    // it instead of the post-correction push-out speed.
    float RelativeSpeed = 0.0f;
    // 0.5 * mass * RelativeSpeed^2, in the engine's pixel/kilogram units.
    float KineticEnergy = 0.0f;
    float Mass = 0.0f;
    std::uint32_t Sequence = 0;
};

static_assert(std::is_trivially_copyable_v<SdfContact>);

// One point of a contact manifold. A body resolves against up to four points so
// a box in a corner cannot flip between two normals from step to step.
struct SdfContactPoint
{
    Vector2D Point{};
    Vector2D Normal{0.0f, -1.0f};
    float Penetration = 0.0f;
    float Impulse = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SdfContactPoint>);

// The per-body manifold a solver would consume. Fixed size, no allocation.
struct SdfContactManifold
{
    Entity EntityHandle = Fby::kNullEntity;
    std::uint32_t Count = 0;
    std::array<SdfContactPoint, 4> Points{};
};

static_assert(std::is_trivially_copyable_v<SdfContactManifold>);

class SdfBrickMap;
struct SdfBrickHeader;

class SdfCollisionSystem
{
public:
    static constexpr std::uint32_t kMaxContacts = 1024;
    static constexpr std::uint32_t kMaxManifolds = 256;

    SdfCollisionSystem() = default;

    void Initialize(const SdfCollisionDesc& InDesc = {}) noexcept;
    void SetField(const SdfField& InField) noexcept;
    [[nodiscard]] const SdfField& Field() const noexcept
    {
        return MField;
    }

    // Call from Phase::FixedUpdate with the constant fixed dt. Returns the number
    std::uint32_t Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept;
    // Narrow FixedUpdate view: registry, physics bundle and dt.
    std::uint32_t Update(Ecs::PhysicsSystemView& InView) noexcept;

    [[nodiscard]] std::span<const SdfContact> Contacts() const noexcept
    {
        return {MContacts.data(), MContactCount};
    }
    [[nodiscard]] std::span<const SdfContactManifold> Manifolds() const noexcept
    {
        return {MManifolds.data(), MManifoldCount};
    }
    // Optional broadphase: a body whose centre brick is uniform outside and whose
    // minimum distance exceeds the collider reach is skipped without sampling.
    void SetBrickMap(const SdfBrickMap* InBricks) noexcept
    {
        MBricks = InBricks;
    }
    [[nodiscard]] std::uint32_t BroadphaseSkipped() const noexcept
    {
        return MBroadphaseSkipped;
    }
    [[nodiscard]] std::uint32_t LastCorrected() const noexcept
    {
        return MLastCorrected;
    }
    [[nodiscard]] std::uint32_t DroppedContacts() const noexcept
    {
        return MDroppedContacts;
    }
    [[nodiscard]] bool Register(Ecs::SystemGraph& InGraph);

private:
    static void Tick(Ecs::PhysicsSystemView& InView, void* InUser);

    SdfField MField{};
    SdfCollisionDesc MDesc{};
    std::array<SdfContact, kMaxContacts> MContacts{};
    std::uint32_t MContactCount = 0;
    std::array<SdfContactManifold, kMaxManifolds> MManifolds{};
    std::uint32_t MManifoldCount = 0;
    const SdfBrickMap* MBricks = nullptr;
    std::uint32_t MBroadphaseSkipped = 0;
    std::uint32_t MSequence = 0;
    std::uint32_t MLastCorrected = 0;
    std::uint32_t MDroppedContacts = 0;
};
}
