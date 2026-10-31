// FemboyEngine2D - 2.5D camera stack.
#pragma once

#include "Engine/Core/Mat4.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/PostProcess.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace Fby
{

enum CameraStackFlags : std::uint32_t
{
    CameraStackNone = 0,
    CameraStackFollowX = 1u << 0,
    CameraStackFollowY = 1u << 1,
    CameraStackShake = 1u << 2,
    // Copy the resolved centre into Camera2D
    CameraStackWriteBack = 1u << 3,
};

struct CameraStackDesc
{
    Entity cameraEntity = entt::null;
    Entity followTarget = entt::null;
    Vector2D followOffset{};
    // Higher = tighter follow
    float followSharpness = 6.0f;
    std::uint32_t flags = CameraStackFollowX | CameraStackFollowY | CameraStackShake | CameraStackWriteBack;

    // Authored values used when cameraEntity has no Camera2D component.
    Camera2D camera{};
    float tiltRadians = 0.0f;
    // 0 = pure orthographic tilt
    float perspectiveDistance = 0.0f;

    // Trauma-based screen shake. Decay is per second; the amplitude is applied
    float shakeAmplitude = 10.0f;
    float shakeFrequency = 18.0f;
    float shakeDecay = 1.8f;
    std::uint32_t seed = 0xC0FFEEu;

    // Per-camera post policy. A gameplay camera typically leaves CRT off while a
    Rhi::PostFxSettings postFx{};
};

class CameraStackSystem
{
public:
    static constexpr std::uint32_t kMaxCameras = 8;

    bool Push(Ecs::Registry& InRegistry, const CameraStackDesc& InDesc) noexcept;
    bool Pop() noexcept;
    void Clear() noexcept;

    // Follows targets, applies shake and rebuilds the tilted projection. Call
    void Update(Ecs::SystemContext& InContext) noexcept;

    [[nodiscard]] std::uint32_t Depth() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCount == 0;
    }
    [[nodiscard]] const Camera2D& ActiveCamera() const noexcept;
    [[nodiscard]] Mat4 ViewProjection() const noexcept;
    // The matrix the renderer should use for a frame that is between two
    [[nodiscard]] Mat4 InterpolatedProjection(float InAlpha) const noexcept;

    // Adds trauma to the top camera. This is an impulse, not a per-frame state:
    void AddTrauma(float InAmount) noexcept;
    void SetTilt(float InRadians) noexcept;
    void SetFollowTarget(Entity InEntity) noexcept;
    // Display DPI scale from SDL_GetWindowDisplayScale. The UI uses it to convert
    void SetDisplayScale(float InScale) noexcept;
    [[nodiscard]] float DisplayScale() const noexcept
    {
        return MDisplayScale;
    }
    // Replaces the top camera's post-process policy. The render graph reads the
    void SetPostFx(const Rhi::PostFxSettings& InSettings) noexcept;
    [[nodiscard]] const Rhi::PostFxSettings& ActivePostFx() const noexcept;

    [[nodiscard]] static Mat4 BuildProjection(const Camera2D& InCamera, float InTiltRadians,
                                              float InPerspectiveDistance) noexcept;
    [[nodiscard]] static Mat4 LerpProjection(const Mat4& InFrom, const Mat4& InTo, float InAlpha) noexcept;

private:
    struct Slot
    {
        bool active = false;
        CameraStackDesc desc{};
        // Follow/tilt state without shake
        Camera2D baseCamera{};
        // Resolved camera returned to gameplay
        Camera2D camera{};
        Mat4 previousProjection = Mat4::Identity();
        Mat4 currentProjection = Mat4::Identity();
        float trauma = 0.0f;
        float phaseX = 0.0f;
        float phaseY = 0.0f;
        bool projectionPrimed = false;
    };

    [[nodiscard]] Slot* Top() noexcept;
    [[nodiscard]] const Slot* Top() const noexcept;

    std::array<Slot, kMaxCameras> MSlots{};
    std::uint32_t MCount = 0;
    Rhi::PostFxSettings MDefaultPostFx{};
    float MDisplayScale = 1.0f;
};
}
