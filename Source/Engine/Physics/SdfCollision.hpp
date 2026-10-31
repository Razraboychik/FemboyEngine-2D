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

    [[nodiscard]] bool Valid() const noexcept
    {
        return samples != nullptr && width > 1 && height > 1;
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
    // Extra probes along each collider axis, 0 = centre + corners
    std::uint32_t subSamples = 1;
    // Skip sensors and static/kinematic bodies
    bool dynamicOnly = true;
};

struct SdfContact
{
    Entity entity = entt::null;
    Vector2D point{};
    Vector2D normal{0.0f, -1.0f};
    float penetration = 0.0f;
    float impulse = 0.0f;
    std::uint32_t sequence = 0;
};

static_assert(std::is_trivially_copyable_v<SdfContact>);

class SdfCollisionSystem
{
public:
    static constexpr std::uint32_t kMaxContacts = 1024;

    SdfCollisionSystem() = default;

    void Initialize(const SdfCollisionDesc& InDesc = {}) noexcept;
    void SetField(const SdfField& InField) noexcept;
    [[nodiscard]] const SdfField& Field() const noexcept
    {
        return MField;
    }

    // Call from Phase::FixedUpdate with the constant fixed dt. Returns the number
    std::uint32_t Step(Ecs::Registry& InRegistry, PhysicsSystem& InPhysics, float InFixedDt) noexcept;
    // ECS convenience: uses context.physics and context.dt, clamped to a safe
    std::uint32_t Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] std::span<const SdfContact> Contacts() const noexcept
    {
        return {MContacts.data(), MContactCount};
    }
    [[nodiscard]] std::uint32_t LastCorrected() const noexcept
    {
        return MLastCorrected;
    }
    [[nodiscard]] bool Register(Ecs::SystemGraph& InGraph);

private:
    static void Tick(Ecs::SystemContext& InContext, void* InUser);

    SdfField MField{};
    SdfCollisionDesc MDesc{};
    std::array<SdfContact, kMaxContacts> MContacts{};
    std::uint32_t MContactCount = 0;
    std::uint32_t MSequence = 0;
    std::uint32_t MLastCorrected = 0;
};
}
