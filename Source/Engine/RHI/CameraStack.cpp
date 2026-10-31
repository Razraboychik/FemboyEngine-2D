// FemboyEngine2D - camera stack implementation.
#include "Engine/RHI/CameraStack.hpp"

#include "Engine/Core/Pcg32.hpp"
#include "Engine/Core/WindowManager.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

[[nodiscard]] float ApproachFactor(float InSharpness, float InDt) noexcept
{
    if (InSharpness <= 0.0f || InDt <= 0.0f)
    {
        return 0.0f;
    }
    return 1.0f - std::exp(-InSharpness * InDt);
}
}

CameraStackSystem::Slot* CameraStackSystem::Top() noexcept
{
    return MCount == 0 ? nullptr : &MSlots[MCount - 1];
}

const CameraStackSystem::Slot* CameraStackSystem::Top() const noexcept
{
    return MCount == 0 ? nullptr : &MSlots[MCount - 1];
}

bool CameraStackSystem::Push(Ecs::Registry& InRegistry, const CameraStackDesc& InDesc) noexcept
{
    if (MCount >= kMaxCameras)
    {
        return false;
    }

    Slot& ScopedSlot = MSlots[MCount];
    ScopedSlot = Slot{};
    ScopedSlot.desc = InDesc;
    ScopedSlot.camera = InDesc.camera;
    ScopedSlot.baseCamera = InDesc.camera;
    if (InDesc.cameraEntity != entt::null && InRegistry.valid(InDesc.cameraEntity))
    {
        if (const auto* ScopedAuthored = InRegistry.try_get<Camera2D>(InDesc.cameraEntity); ScopedAuthored != nullptr)
        {
            ScopedSlot.camera = *ScopedAuthored;
            ScopedSlot.baseCamera = *ScopedAuthored;
        }
    }

    if (InDesc.followTarget != entt::null && InRegistry.valid(InDesc.followTarget))
    {
        if (const auto* ScopedTarget = InRegistry.try_get<Transform2D>(InDesc.followTarget); ScopedTarget != nullptr)
        {
            ScopedSlot.camera.Center = ScopedTarget->Position + InDesc.followOffset;
            ScopedSlot.baseCamera.Center = ScopedSlot.camera.Center;
        }
    }

    Pcg32 ScopedRng = Pcg32::FromSeed(InDesc.seed);
    ScopedSlot.phaseX = ScopedRng.NextFloat() * kTwoPi;
    ScopedSlot.phaseY = ScopedRng.NextFloat() * kTwoPi;
    ScopedSlot.currentProjection = BuildProjection(ScopedSlot.camera, InDesc.tiltRadians, InDesc.perspectiveDistance);
    ScopedSlot.previousProjection = ScopedSlot.currentProjection;
    ScopedSlot.projectionPrimed = true;
    ScopedSlot.active = true;
    ++MCount;
    return true;
}

bool CameraStackSystem::Pop() noexcept
{
    if (MCount == 0)
    {
        return false;
    }
    --MCount;
    MSlots[MCount] = Slot{};
    return true;
}

void CameraStackSystem::Clear() noexcept
{
    MCount = 0;
    MSlots.fill(Slot{});
}

void CameraStackSystem::Update(Ecs::SystemContext& InContext) noexcept
{
    if (InContext.window != nullptr)
    {
        void* ScopedNative = InContext.window->NativeWindow();
        if (ScopedNative != nullptr)
        {
            const float ScopedScale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(ScopedNative));
            if (ScopedScale > 0.0f)
            {
                SetDisplayScale(ScopedScale);
            }
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MCount; ++ScopedI)
    {
        Slot& ScopedSlot = MSlots[ScopedI];
        if (!ScopedSlot.active)
        {
            continue;
        }

        Camera2D ScopedBase = ScopedSlot.baseCamera;

        // Pick up authoring changes (zoom, viewport) without overwriting the
        if (ScopedSlot.desc.cameraEntity != entt::null && InContext.registry.valid(ScopedSlot.desc.cameraEntity))
        {
            if (const auto* ScopedAuthored = InContext.registry.try_get<Camera2D>(ScopedSlot.desc.cameraEntity);
                ScopedAuthored != nullptr)
            {
                ScopedBase.ViewSize = ScopedAuthored->ViewSize;
                ScopedBase.Zoom = ScopedAuthored->Zoom;
                if ((ScopedSlot.desc.flags & CameraStackWriteBack) == 0)
                {
                    ScopedBase.Rotation = ScopedAuthored->Rotation;
                }
            }
        }

        if (ScopedSlot.desc.followTarget != entt::null && InContext.registry.valid(ScopedSlot.desc.followTarget))
        {
            if (const auto* ScopedTarget = InContext.registry.try_get<Transform2D>(ScopedSlot.desc.followTarget);
                ScopedTarget != nullptr)
            {
                const Vector2D ScopedDesired = ScopedTarget->Position + ScopedSlot.desc.followOffset;
                const float ScopedFactor = ApproachFactor(ScopedSlot.desc.followSharpness, InContext.dt);
                if ((ScopedSlot.desc.flags & CameraStackFollowX) != 0)
                {
                    ScopedBase.Center.X += (ScopedDesired.X - ScopedBase.Center.X) * ScopedFactor;
                }
                if ((ScopedSlot.desc.flags & CameraStackFollowY) != 0)
                {
                    ScopedBase.Center.Y += (ScopedDesired.Y - ScopedBase.Center.Y) * ScopedFactor;
                }
            }
        }

        // Shake is applied on top of the unshaken base, never accumulated into
        Camera2D ScopedResolved = ScopedBase;
        if ((ScopedSlot.desc.flags & CameraStackShake) != 0 && InContext.dt > 0.0f)
        {
            ScopedSlot.trauma = std::max(0.0f, ScopedSlot.trauma - ScopedSlot.desc.shakeDecay * InContext.dt);
            const float ScopedIntensity = ScopedSlot.trauma * ScopedSlot.trauma;
            if (ScopedIntensity > 0.0f)
            {
                ScopedSlot.phaseX += InContext.dt * ScopedSlot.desc.shakeFrequency * kTwoPi;
                ScopedSlot.phaseY += InContext.dt * ScopedSlot.desc.shakeFrequency * 1.37f * kTwoPi;
                const float ScopedAmplitude = ScopedSlot.desc.shakeAmplitude * ScopedIntensity;
                // Vertical is intentionally a little softer than horizontal;
                ScopedResolved.Center.X += std::sin(ScopedSlot.phaseX) * ScopedAmplitude;
                ScopedResolved.Center.Y += std::sin(ScopedSlot.phaseY) * ScopedAmplitude * 0.78f;
                ScopedResolved.Rotation += std::sin(ScopedSlot.phaseX * 0.63f + 1.1f) * 0.012f * ScopedIntensity;
            }
        }

        // `previousProjection` is the last rendered frame. A frame recorded
        if (ScopedSlot.projectionPrimed)
        {
            ScopedSlot.previousProjection = ScopedSlot.currentProjection;
        }
        ScopedSlot.currentProjection =
            BuildProjection(ScopedResolved, ScopedSlot.desc.tiltRadians, ScopedSlot.desc.perspectiveDistance);
        ScopedSlot.projectionPrimed = true;

        if ((ScopedSlot.desc.flags & CameraStackWriteBack) != 0 && ScopedSlot.desc.cameraEntity != entt::null &&
            InContext.registry.valid(ScopedSlot.desc.cameraEntity))
        {
            if (auto* ScopedAuthored = InContext.registry.try_get<Camera2D>(ScopedSlot.desc.cameraEntity);
                ScopedAuthored != nullptr)
            {
                ScopedAuthored->Center = ScopedResolved.Center;
                ScopedAuthored->Rotation = ScopedResolved.Rotation;
            }
        }

        ScopedSlot.baseCamera = ScopedBase;
        ScopedSlot.camera = ScopedResolved;
    }
}

const Camera2D& CameraStackSystem::ActiveCamera() const noexcept
{
    static const Camera2D ScopedKFallback{};
    const Slot* ScopedSlot = Top();
    return ScopedSlot == nullptr ? ScopedKFallback : ScopedSlot->camera;
}

Mat4 CameraStackSystem::ViewProjection() const noexcept
{
    const Slot* ScopedSlot = Top();
    return ScopedSlot == nullptr ? Mat4::Identity() : ScopedSlot->currentProjection;
}

Mat4 CameraStackSystem::InterpolatedProjection(float InAlpha) const noexcept
{
    const Slot* ScopedSlot = Top();
    if (ScopedSlot == nullptr)
    {
        return Mat4::Identity();
    }
    return LerpProjection(ScopedSlot->previousProjection, ScopedSlot->currentProjection, InAlpha);
}

void CameraStackSystem::AddTrauma(float InAmount) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->trauma = std::clamp(ScopedSlot->trauma + InAmount, 0.0f, 1.0f);
    }
}

void CameraStackSystem::SetTilt(float InRadians) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->desc.tiltRadians = std::clamp(InRadians, -1.35f, 1.35f);
    }
}

void CameraStackSystem::SetFollowTarget(Entity InEntity) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->desc.followTarget = InEntity;
    }
}

void CameraStackSystem::SetDisplayScale(float InScale) noexcept
{
    MDisplayScale = std::clamp(InScale, 0.5f, 4.0f);
}

void CameraStackSystem::SetPostFx(const Rhi::PostFxSettings& InSettings) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->desc.postFx = InSettings;
    }
    else
    {
        MDefaultPostFx = InSettings;
    }
}

const Rhi::PostFxSettings& CameraStackSystem::ActivePostFx() const noexcept
{
    const Slot* ScopedSlot = Top();
    return ScopedSlot == nullptr ? MDefaultPostFx : ScopedSlot->desc.postFx;
}

Mat4 CameraStackSystem::BuildProjection(const Camera2D& InCamera, float InTiltRadians,
                                        float InPerspectiveDistance) noexcept
{
    Mat4 ScopedResult{};
    ScopedResult.m.fill(0.0f);

    const float ScopedViewWidth = std::max(InCamera.ViewSize.X, 1.0f);
    const float ScopedViewHeight = std::max(InCamera.ViewSize.Y, 1.0f);
    const float ScopedZoom = std::max(InCamera.Zoom, 0.0001f);
    const float ScopedTilt = std::clamp(InTiltRadians, -1.35f, 1.35f);
    const float ScopedCosine = std::cos(InCamera.Rotation);
    const float ScopedSine = std::sin(InCamera.Rotation);
    const float ScopedScaleX = 2.0f * ScopedZoom / ScopedViewWidth;
    const float ScopedScaleY = 2.0f * ScopedZoom / ScopedViewHeight;

    // Rotate around the camera centre, then scale to NDC. Column-major:
    ScopedResult.m[0] = ScopedScaleX * ScopedCosine;
    ScopedResult.m[4] = ScopedScaleX * ScopedSine;
    ScopedResult.m[12] = -ScopedScaleX * (ScopedCosine * InCamera.Center.X + ScopedSine * InCamera.Center.Y);
    ScopedResult.m[1] = -ScopedScaleY * ScopedSine * std::cos(ScopedTilt);
    ScopedResult.m[5] = ScopedScaleY * ScopedCosine * std::cos(ScopedTilt);
    ScopedResult.m[13] =
        ScopedScaleY * (ScopedSine * InCamera.Center.X - ScopedCosine * InCamera.Center.Y) * std::cos(ScopedTilt);

    // Perspective term in the w row. With distance <= 0 this stays exactly
    const float ScopedWSlope =
        InPerspectiveDistance > 0.0f ? std::sin(ScopedTilt) / std::max(InPerspectiveDistance, 1.0f) : 0.0f;
    ScopedResult.m[7] = ScopedWSlope;
    ScopedResult.m[15] = 1.0f - ScopedWSlope * InCamera.Center.Y;
    ScopedResult.m[10] = 1.0f;
    return ScopedResult;
}

Mat4 CameraStackSystem::LerpProjection(const Mat4& InFrom, const Mat4& InTo, float InAlpha) noexcept
{
    const float ScopedBlend = std::clamp(InAlpha, 0.0f, 1.0f);
    if (ScopedBlend <= 0.0f)
    {
        return InFrom;
    }
    if (ScopedBlend >= 1.0f)
    {
        return InTo;
    }
    Mat4 ScopedResult{};
    for (std::size_t ScopedI = 0; ScopedI < ScopedResult.m.size(); ++ScopedI)
    {
        ScopedResult.m[ScopedI] = InFrom.m[ScopedI] + (InTo.m[ScopedI] - InFrom.m[ScopedI]) * ScopedBlend;
    }
    return ScopedResult;
}
}
