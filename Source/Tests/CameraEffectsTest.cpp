// FemboyEngine2D - generic camera motion and screen-distortion primitives.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/CameraStack.hpp"
#include "Engine/UI/Accessibility.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Fby;

namespace
{
void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        std::exit(1);
    }
}

bool Near(float InA, float InB, float InEpsilon)
{
    return std::abs(InA - InB) <= InEpsilon;
}
}

int main()
{
    Ecs::Registry ScopedRegistry;
    const Entity ScopedTarget = ScopedRegistry.create();
    ScopedRegistry.emplace<Transform2D>(ScopedTarget);
    const Entity ScopedCamera = ScopedRegistry.create();
    Camera2D ScopedAuthored;
    ScopedAuthored.Center = Vector2D{320.0f, 180.0f};
    ScopedAuthored.ViewSize = Vector2D{640.0f, 360.0f};
    ScopedRegistry.emplace<Camera2D>(ScopedCamera, ScopedAuthored);

    JobSystem ScopedJobs(1);
    Ecs::SystemContext ScopedContext{ScopedRegistry, ScopedJobs};
    ScopedContext.dt = 1.0f / 60.0f;
    CameraStackSystem ScopedCameras;
    CameraStackDesc ScopedDesc;
    ScopedDesc.cameraEntity = ScopedCamera;
    ScopedDesc.followTarget = ScopedTarget;
    ScopedDesc.camera = ScopedAuthored;
    Check(ScopedCameras.Push(ScopedRegistry, ScopedDesc), "camera pushes");
    ScopedCameras.Update(ScopedContext);

    // Motion scale zero must return exactly the unshaken base, even with trauma.
    ScopedCameras.SetMotionScale(0.0f);
    ScopedCameras.AddTrauma(1.0f);
    CameraDistortionSettings ScopedDistortion;
    ScopedDistortion.flags = CameraDistortionDoubleVision | CameraDistortionStretch;
    ScopedDistortion.doubleVision = 0.5f;
    ScopedDistortion.stretchX = 1.3f;
    ScopedDistortion.stretchY = 0.7f;
    ScopedCameras.SetDistortion(ScopedDistortion, 4.0f);
    ScopedCameras.Update(ScopedContext);
    Check(Near(ScopedCameras.ActiveCamera().Center.X, ScopedCameras.BaseCamera().Center.X, 0.001f), "motion scale zero keeps X");
    Check(Near(ScopedCameras.ActiveCamera().Center.Y, ScopedCameras.BaseCamera().Center.Y, 0.001f), "motion scale zero keeps Y");
    Check(Near(ScopedCameras.ActiveCamera().Rotation, ScopedCameras.BaseCamera().Rotation, 0.001f), "motion scale zero keeps rotation");
    Check(ScopedCameras.ActiveDistortion().doubleVision == 0.0f, "motion scale zero disables distortion");

    ScopedCameras.SetMotionScale(1.0f);
    ScopedCameras.SetOffset(Vector2D{10.0f, -6.0f}, 0.5f);
    ScopedCameras.SetZoom(1.1f, 0.5f);
    ScopedCameras.SetRoll(0.1f, 0.5f);
    ScopedCameras.Update(ScopedContext);
    Check(ScopedCameras.ActiveCamera().Center.X != ScopedCameras.BaseCamera().Center.X, "offset changes the resolved camera");
    Check(ScopedCameras.ActiveCamera().Zoom > 1.0f, "zoom changes the resolved camera");
    Check(std::abs(ScopedCameras.ActiveCamera().Rotation) > 0.0f, "roll changes the resolved camera");
    const CameraDistortionSettings ScopedActive = ScopedCameras.ActiveDistortion();
    Check((ScopedActive.flags & CameraDistortionDoubleVision) != 0, "double vision is active");
    Check((ScopedCameras.ActivePostFx().flags & Rhi::PostFxCameraDistortion) != 0,
          "distortion requests the postfx pass");
    Check(ScopedActive.stretchX > 1.0f && ScopedActive.stretchY < 1.0f, "anisotropic stretch is active");

    // Writeback stores the unshaken base, never the resolved render camera.
    Camera2D* ScopedWritten = ScopedRegistry.try_get<Camera2D>(ScopedCamera);
    Check(ScopedWritten != nullptr, "camera component exists");
    Check(Near(ScopedWritten->Center.X, ScopedCameras.BaseCamera().Center.X, 0.01f),
          "writeback excludes scripted offset");

    // The accessibility motion scale multiplies the scripted scale.
    AccessibilityKernel ScopedAccessibility;
    AccessibilityProfile ScopedProfile = DefaultAccessibilityProfile();
    ScopedProfile.presentation.motionScale = 0.0f;
    ScopedAccessibility.ApplyProfile(ScopedProfile);
    ScopedContext.accessibility = &ScopedAccessibility;
    ScopedCameras.SetMotionScale(1.0f);
    ScopedCameras.AddTrauma(1.0f);
    ScopedCameras.Update(ScopedContext);
    Check(Near(ScopedCameras.ActiveCamera().Center.X, ScopedCameras.BaseCamera().Center.X, 0.001f),
          "accessibility motion scale zero keeps the base");

    ScopedCameras.ClearEffects();
    ScopedCameras.Update(ScopedContext);
    Check(Near(ScopedCameras.ActiveCamera().Center.X, ScopedCameras.BaseCamera().Center.X, 0.01f),
          "clear returns to the base");
    Check(ScopedCameras.ActiveDistortion().flags == CameraDistortionNone, "clear removes distortion");
    Check(ScopedCameras.Trauma() == 0.0f, "clear removes trauma");
    Check(ScopedCameras.Pop(), "camera pops");
    std::printf("camera_effects: ok\n");
    return 0;
}
