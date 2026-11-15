// FemboyEngine2D - camera stack implementation.
#include "Engine/RHI/CameraStack.hpp"

#include "Engine/Core/Pcg32.hpp"
#include "Engine/Core/SimMath.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/UI/Accessibility.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace Fby
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

[[nodiscard]] std::uint32_t HashNoise(std::uint32_t InValue) noexcept
{
    InValue ^= InValue >> 16u;
    InValue *= 0x7FEB352Du;
    InValue ^= InValue >> 15u;
    InValue *= 0x846CA68Bu;
    InValue ^= InValue >> 16u;
    return InValue;
}

[[nodiscard]] float ValueNoise(float InPhase, std::uint32_t InSeed) noexcept
{
    const float ScopedFloor = std::floor(InPhase);
    const auto ScopedCell = static_cast<std::uint32_t>(static_cast<std::int32_t>(ScopedFloor));
    const float ScopedFraction = InPhase - ScopedFloor;
    const float ScopedSmooth = ScopedFraction * ScopedFraction * (3.0f - 2.0f * ScopedFraction);
    const auto ScopedSample = [InSeed](std::uint32_t InCell) noexcept
    {
        return static_cast<float>(HashNoise(InCell ^ InSeed) & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    };
    const float ScopedA = ScopedSample(ScopedCell);
    const float ScopedB = ScopedSample(ScopedCell + 1u);
    return (ScopedA + (ScopedB - ScopedA) * ScopedSmooth) * 2.0f - 1.0f;
}

[[nodiscard]] float TimedWeight(float InRemaining, float InDuration) noexcept
{
    if (InDuration <= 0.0f)
    {
        return InRemaining < 0.0f ? 1.0f : 0.0f;
    }
    const float ScopedT = std::clamp(InRemaining / InDuration, 0.0f, 1.0f);
    return ScopedT * ScopedT * (3.0f - 2.0f * ScopedT);
}

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
    if (InDesc.cameraEntity != Fby::kNullEntity && InRegistry.valid(InDesc.cameraEntity))
    {
        if (const auto* ScopedAuthored = InRegistry.try_get<Camera2D>(InDesc.cameraEntity); ScopedAuthored != nullptr)
        {
            ScopedSlot.camera = *ScopedAuthored;
            ScopedSlot.baseCamera = *ScopedAuthored;
        }
    }

    if (InDesc.followTarget != Fby::kNullEntity && InRegistry.valid(InDesc.followTarget))
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
    ScopedSlot.previousCamera = ScopedSlot.camera;
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

void CameraStackSystem::Update(Ecs::RenderSystemView& InView) noexcept
{
    if (InView.services.window != nullptr)
    {
        void* ScopedNative = InView.services.window->NativeWindow();
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

        // Keep last frame's resolved camera so the renderer can interpolate the
        // camera parameters rather than the projection matrix.
        ScopedSlot.previousCamera = ScopedSlot.camera;
        Camera2D ScopedBase = ScopedSlot.baseCamera;

        // Pick up authoring changes (zoom, viewport) without overwriting.
        if (ScopedSlot.desc.cameraEntity != Fby::kNullEntity && InView.registry.valid(ScopedSlot.desc.cameraEntity))
        {
            if (const auto* ScopedAuthored = InView.registry.try_get<Camera2D>(ScopedSlot.desc.cameraEntity);
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

        if (ScopedSlot.desc.followTarget != Fby::kNullEntity && InView.registry.valid(ScopedSlot.desc.followTarget))
        {
            if (const auto* ScopedTarget = InView.registry.try_get<Transform2D>(ScopedSlot.desc.followTarget);
                ScopedTarget != nullptr)
            {
                const Vector2D ScopedDesired = ScopedTarget->Position + ScopedSlot.desc.followOffset;
                const float ScopedFactor = ApproachFactor(ScopedSlot.desc.followSharpness, InView.dt);
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

        // Effects are layered on the unshaken base. The base is what gameplay
        // and scripts read; the resolved pose is render-only.
        Camera2D ScopedResolved = ScopedBase;
        float ScopedAccessibilityScale = 1.0f;
        if (InView.accessibilityServices.accessibility != nullptr)
        {
            ScopedAccessibilityScale = InView.accessibilityServices.accessibility->Profile().presentation.motionScale;
        }
        MEffectiveMotionScale = std::clamp(MMotionScale * ScopedAccessibilityScale, 0.0f, 1.0f);
        const float ScopedMotion = MEffectiveMotionScale;

        if (ScopedSlot.timedOffsetRemaining != 0.0f)
        {
            const float ScopedWeight = TimedWeight(ScopedSlot.timedOffsetRemaining, ScopedSlot.timedOffsetDuration);
            ScopedResolved.Center += ScopedSlot.timedOffset * (ScopedWeight * ScopedMotion);
            if (ScopedSlot.timedOffsetDuration > 0.0f)
            {
                ScopedSlot.timedOffsetRemaining =
                    std::max(0.0f, ScopedSlot.timedOffsetRemaining - InView.dt);
            }
        }
        if (ScopedSlot.timedZoomRemaining != 0.0f)
        {
            const float ScopedWeight = TimedWeight(ScopedSlot.timedZoomRemaining, ScopedSlot.timedZoomDuration);
            const float ScopedZoom = 1.0f + (ScopedSlot.timedZoom - 1.0f) * ScopedWeight * ScopedMotion;
            ScopedResolved.Zoom *= ScopedZoom;
            if (ScopedSlot.timedZoomDuration > 0.0f)
            {
                ScopedSlot.timedZoomRemaining = std::max(0.0f, ScopedSlot.timedZoomRemaining - InView.dt);
            }
        }
        if (ScopedSlot.timedRollRemaining != 0.0f)
        {
            const float ScopedWeight = TimedWeight(ScopedSlot.timedRollRemaining, ScopedSlot.timedRollDuration);
            ScopedResolved.Rotation += ScopedSlot.timedRoll * ScopedWeight * ScopedMotion;
            if (ScopedSlot.timedRollDuration > 0.0f)
            {
                ScopedSlot.timedRollRemaining = std::max(0.0f, ScopedSlot.timedRollRemaining - InView.dt);
            }
        }

        if ((ScopedSlot.desc.flags & CameraStackShake) != 0 && InView.dt > 0.0f)
        {
            ScopedSlot.trauma = std::max(0.0f, ScopedSlot.trauma - ScopedSlot.desc.shakeDecay * InView.dt);
            const float ScopedIntensity = ScopedSlot.trauma * ScopedSlot.trauma * ScopedMotion;
            if (ScopedIntensity > 0.0f)
            {
                ScopedSlot.phaseX += InView.dt * ScopedSlot.desc.shakeFrequency * kTwoPi;
                ScopedSlot.phaseY += InView.dt * ScopedSlot.desc.shakeFrequency * 1.37f * kTwoPi;
                ScopedSlot.phaseRoll += InView.dt * ScopedSlot.desc.shakeFrequency * 0.63f * kTwoPi;
                const float ScopedAmplitude =
                    ScopedSlot.desc.shakeAmplitude * ScopedIntensity * MComfortShakeScale;
                ScopedResolved.Center.X += ValueNoise(ScopedSlot.phaseX, ScopedSlot.desc.seed) * ScopedAmplitude;
                ScopedResolved.Center.Y += ValueNoise(ScopedSlot.phaseY, ScopedSlot.desc.seed ^ 0x9E3779B9u) *
                                           ScopedAmplitude * 0.78f;
                ScopedResolved.Rotation +=
                    ValueNoise(ScopedSlot.phaseRoll, ScopedSlot.desc.seed ^ 0x85EBCA6Bu) * 0.012f * ScopedIntensity;
            }
        }

        if (ScopedSlot.distortionRemaining != 0.0f && ScopedSlot.distortionDuration > 0.0f)
        {
            ScopedSlot.distortionRemaining = std::max(0.0f, ScopedSlot.distortionRemaining - InView.dt);
            if (ScopedSlot.distortionRemaining == 0.0f)
            {
                ScopedSlot.distortion = CameraDistortionSettings{};
                ScopedSlot.distortionDuration = 0.0f;
                ScopedSlot.desc.postFx.flags &= ~Rhi::PostFxCameraDistortion;
            }
        }

        // Projection history is render-only; writeback below stores the base.
        if (ScopedSlot.projectionPrimed)
        {
            ScopedSlot.previousProjection = ScopedSlot.currentProjection;
        }
        ScopedSlot.currentProjection =
            BuildProjection(ScopedResolved, ScopedSlot.desc.tiltRadians, ScopedSlot.desc.perspectiveDistance);
        ScopedSlot.projectionPrimed = true;

        if ((ScopedSlot.desc.flags & CameraStackWriteBack) != 0 && ScopedSlot.desc.cameraEntity != Fby::kNullEntity &&
            InView.registry.valid(ScopedSlot.desc.cameraEntity))
        {
            if (auto* ScopedAuthored = InView.registry.try_get<Camera2D>(ScopedSlot.desc.cameraEntity);
                ScopedAuthored != nullptr)
            {
                ScopedAuthored->Center = ScopedBase.Center;
                ScopedAuthored->Rotation = ScopedBase.Rotation;
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
    // Interpolate the camera parameters, then build the matrix: blending matrix
    // elements distorts rotation and perspective. The clamp keeps the result
    // inside the authored projection range.
    const float ScopedBlend = std::clamp(InAlpha, 0.0f, 1.0f);
    Camera2D ScopedCamera = ScopedSlot->camera;
    ScopedCamera.Center =
        ScopedSlot->previousCamera.Center +
        (ScopedSlot->camera.Center - ScopedSlot->previousCamera.Center) * ScopedBlend;
    ScopedCamera.ViewSize =
        ScopedSlot->previousCamera.ViewSize +
        (ScopedSlot->camera.ViewSize - ScopedSlot->previousCamera.ViewSize) * ScopedBlend;
    ScopedCamera.Zoom =
        ScopedSlot->previousCamera.Zoom + (ScopedSlot->camera.Zoom - ScopedSlot->previousCamera.Zoom) * ScopedBlend;
    ScopedCamera.Rotation = ScopedSlot->previousCamera.Rotation +
                            (ScopedSlot->camera.Rotation - ScopedSlot->previousCamera.Rotation) * ScopedBlend;
    return BuildProjection(ScopedCamera, ScopedSlot->desc.tiltRadians, ScopedSlot->desc.perspectiveDistance);
}

void CameraStackSystem::AddTrauma(float InAmount) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->trauma = std::clamp(ScopedSlot->trauma + InAmount, 0.0f, 1.0f);
    }
}

float CameraStackSystem::Trauma() const noexcept
{
    const Slot* ScopedSlot = Top();
    return ScopedSlot == nullptr ? 0.0f : ScopedSlot->trauma;
}

void CameraStackSystem::SetShake(float InAmplitude, float InFrequency, float InDecay) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->desc.shakeAmplitude = std::max(InAmplitude, 0.0f);
        ScopedSlot->desc.shakeFrequency = std::max(InFrequency, 0.0f);
        ScopedSlot->desc.shakeDecay = std::max(InDecay, 0.0f);
    }
}

void CameraStackSystem::SetDistortion(const CameraDistortionSettings& InSettings, float InDuration) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->distortion = InSettings;
        if (InSettings.flags != CameraDistortionNone)
        {
            ScopedSlot->desc.postFx.flags |= Rhi::PostFxCameraDistortion;
        }
        else
        {
            ScopedSlot->desc.postFx.flags &= ~Rhi::PostFxCameraDistortion;
        }
        ScopedSlot->distortionDuration = std::max(InDuration, 0.0f);
        ScopedSlot->distortionRemaining = InDuration > 0.0f ? InDuration : -1.0f;
    }
}

CameraDistortionSettings CameraStackSystem::ActiveDistortion() const noexcept
{
    const Slot* ScopedSlot = Top();
    if (ScopedSlot == nullptr || ScopedSlot->distortion.flags == CameraDistortionNone)
    {
        return CameraDistortionSettings{};
    }
    CameraDistortionSettings ScopedResult = ScopedSlot->distortion;
    const float ScopedWeight = ScopedSlot->distortionDuration > 0.0f
                                   ? TimedWeight(ScopedSlot->distortionRemaining, ScopedSlot->distortionDuration)
                                   : 1.0f;
    const float ScopedMotion = MEffectiveMotionScale;
    const float ScopedScale = ScopedWeight * ScopedMotion;
    ScopedResult.doubleVision *= ScopedScale;
    ScopedResult.radialWarp *= ScopedScale;
    ScopedResult.chromaticSplit *= ScopedScale;
    ScopedResult.wobbleAmplitude *= ScopedScale;
    ScopedResult.stretchX = 1.0f + (ScopedResult.stretchX - 1.0f) * ScopedScale;
    ScopedResult.stretchY = 1.0f + (ScopedResult.stretchY - 1.0f) * ScopedScale;
    return ScopedResult;
}

void CameraStackSystem::SetOffset(Vector2D InOffset, float InDuration) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->timedOffset = InOffset;
        ScopedSlot->timedOffsetDuration = std::max(InDuration, 0.0f);
        ScopedSlot->timedOffsetRemaining = InDuration > 0.0f ? InDuration : -1.0f;
    }
}

void CameraStackSystem::SetZoom(float InZoom, float InDuration) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->timedZoom = std::max(InZoom, 0.01f);
        ScopedSlot->timedZoomDuration = std::max(InDuration, 0.0f);
        ScopedSlot->timedZoomRemaining = InDuration > 0.0f ? InDuration : -1.0f;
    }
}

void CameraStackSystem::SetRoll(float InRadians, float InDuration) noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->timedRoll = InRadians;
        ScopedSlot->timedRollDuration = std::max(InDuration, 0.0f);
        ScopedSlot->timedRollRemaining = InDuration > 0.0f ? InDuration : -1.0f;
    }
}

void CameraStackSystem::ClearEffects() noexcept
{
    if (Slot* ScopedSlot = Top(); ScopedSlot != nullptr)
    {
        ScopedSlot->trauma = 0.0f;
        ScopedSlot->distortion = CameraDistortionSettings{};
        ScopedSlot->desc.postFx.flags &= ~Rhi::PostFxCameraDistortion;
        ScopedSlot->distortionRemaining = 0.0f;
        ScopedSlot->distortionDuration = 0.0f;
        ScopedSlot->timedOffset = Vector2D{};
        ScopedSlot->timedOffsetRemaining = 0.0f;
        ScopedSlot->timedOffsetDuration = 0.0f;
        ScopedSlot->timedZoom = 1.0f;
        ScopedSlot->timedZoomRemaining = 0.0f;
        ScopedSlot->timedZoomDuration = 0.0f;
        ScopedSlot->timedRoll = 0.0f;
        ScopedSlot->timedRollRemaining = 0.0f;
        ScopedSlot->timedRollDuration = 0.0f;
    }
}

void CameraStackSystem::SetMotionScale(float InScale) noexcept
{
    MMotionScale = std::clamp(InScale, 0.0f, 1.0f);
}

const Camera2D& CameraStackSystem::BaseCamera() const noexcept
{
    const Slot* ScopedSlot = Top();
    static const Camera2D ScopedKFallback{};
    return ScopedSlot == nullptr ? ScopedKFallback : ScopedSlot->baseCamera;
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

void CameraStackSystem::SetComfortShakeScale(float InScale) noexcept
{
    MComfortShakeScale = std::clamp(InScale, 0.0f, 1.0f);
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
    const float ScopedCosine = Sim::Cos(InCamera.Rotation);
    const float ScopedSine = Sim::Sin(InCamera.Rotation);
    const float ScopedScaleX = 2.0f * ScopedZoom / ScopedViewWidth;
    const float ScopedScaleY = 2.0f * ScopedZoom / ScopedViewHeight;

    // Rotate around the camera centre, then scale to NDC. Column-major:
    ScopedResult.m[0] = ScopedScaleX * ScopedCosine;
    ScopedResult.m[4] = ScopedScaleX * ScopedSine;
    ScopedResult.m[12] = -ScopedScaleX * (ScopedCosine * InCamera.Center.X + ScopedSine * InCamera.Center.Y);
    ScopedResult.m[1] = -ScopedScaleY * ScopedSine * Sim::Cos(ScopedTilt);
    ScopedResult.m[5] = ScopedScaleY * ScopedCosine * Sim::Cos(ScopedTilt);
    ScopedResult.m[13] =
        ScopedScaleY * (ScopedSine * InCamera.Center.X - ScopedCosine * InCamera.Center.Y) * Sim::Cos(ScopedTilt);

    // Perspective term in the w row. With distance <= 0 this stays exactly
    const float ScopedWSlope =
        InPerspectiveDistance > 0.0f ? Sim::Sin(ScopedTilt) / std::max(InPerspectiveDistance, 1.0f) : 0.0f;
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
