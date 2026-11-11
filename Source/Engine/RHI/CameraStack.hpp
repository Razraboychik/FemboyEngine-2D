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


// Generic camera-screen effects. The engine deliberately does not name a
// gameplay state such as intoxication: a game composes these primitives.
enum CameraDistortionFlags : std::uint32_t
{
    CameraDistortionNone = 0,
    CameraDistortionDoubleVision = 1u << 0,
    CameraDistortionStretch = 1u << 1,
    CameraDistortionRadialWarp = 1u << 2,
    CameraDistortionChromaticSplit = 1u << 3,
    CameraDistortionWobble = 1u << 4,
};

struct CameraDistortionSettings
{
    std::uint32_t flags = CameraDistortionNone;
    // Ghost separation in normalized UV units.
    float doubleVision = 0.0f;
    // Non-uniform UV scale; 1 keeps the image proportional.
    float stretchX = 1.0f;
    float stretchY = 1.0f;
    // Radial lens warp and RGB split.
    float radialWarp = 0.0f;
    float chromaticSplit = 0.0f;
    // Slow low-frequency displacement.
    float wobbleAmplitude = 0.0f;
    float wobbleFrequency = 0.0f;
};

struct CameraStackDesc
{
    Entity cameraEntity = Fby::kNullEntity;
    Entity followTarget = Fby::kNullEntity;
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
    void Update(Ecs::RenderSystemView& InView) noexcept;

    [[nodiscard]] std::uint32_t Depth() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return MCount == 0;
    }
    [[nodiscard]] const Camera2D& ActiveCamera() const noexcept;
    [[nodiscard]] const Camera2D& BaseCamera() const noexcept;
    [[nodiscard]] Mat4 ViewProjection() const noexcept;
    // The matrix the renderer should use for a frame that is between two
    [[nodiscard]] Mat4 InterpolatedProjection(float InAlpha) const noexcept;

    // Adds trauma to the top camera. This is an impulse, not a per-frame state:
    void AddTrauma(float InAmount) noexcept;
    [[nodiscard]] float Trauma() const noexcept;
    void SetShake(float InAmplitude, float InFrequency, float InDecay) noexcept;
    // Generic transient screen effect; duration <= 0 keeps it until replaced.
    void SetDistortion(const CameraDistortionSettings& InSettings, float InDuration = 0.0f) noexcept;
    [[nodiscard]] CameraDistortionSettings ActiveDistortion() const noexcept;
    // Timed offset/zoom/roll: a non-positive duration applies the value at full
    // weight until ClearEffects or another call.
    void SetOffset(Vector2D InOffset, float InDuration = 0.0f) noexcept;
    void SetZoom(float InZoom, float InDuration = 0.0f) noexcept;
    void SetRoll(float InRadians, float InDuration = 0.0f) noexcept;
    void ClearEffects() noexcept;
    // Accessibility multiplier applied to shake, sway, distortion and timed
    // offsets. Zero returns the camera to its exact base each frame.
    void SetMotionScale(float InScale) noexcept;
    [[nodiscard]] float MotionScale() const noexcept
    {
        return MMotionScale;
    }
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
    // 0 disables trauma shake, 1 keeps the authored amplitude.
    void SetComfortShakeScale(float InScale) noexcept;
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
        // Resolved camera of the previous render frame, used for interpolation.
        Camera2D previousCamera{};
        Mat4 previousProjection = Mat4::Identity();
        Mat4 currentProjection = Mat4::Identity();
        float trauma = 0.0f;
        float phaseX = 0.0f;
        float phaseY = 0.0f;
        float phaseRoll = 0.0f;
        CameraDistortionSettings distortion{};
        float distortionRemaining = 0.0f;
        float distortionDuration = 0.0f;
        Vector2D timedOffset{};
        float timedOffsetRemaining = 0.0f;
        float timedOffsetDuration = 0.0f;
        float timedZoom = 1.0f;
        float timedZoomRemaining = 0.0f;
        float timedZoomDuration = 0.0f;
        float timedRoll = 0.0f;
        float timedRollRemaining = 0.0f;
        float timedRollDuration = 0.0f;
        bool projectionPrimed = false;
    };

    [[nodiscard]] Slot* Top() noexcept;
    [[nodiscard]] const Slot* Top() const noexcept;

    std::array<Slot, kMaxCameras> MSlots{};
    std::uint32_t MCount = 0;
    Rhi::PostFxSettings MDefaultPostFx{};
    float MComfortShakeScale = 1.0f;
    float MDisplayScale = 1.0f;
    float MMotionScale = 1.0f;
    float MEffectiveMotionScale = 1.0f;
};
}
