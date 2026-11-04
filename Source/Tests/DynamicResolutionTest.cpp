// FemboyEngine2D - dynamic render scale controller tests.
#include "Engine/RHI/DynamicResolution.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (0)
}

int main()
{
    Fby::Rhi::DynamicResolutionSettings ScopedSettings;
    ScopedSettings.minScale = 0.5f;
    ScopedSettings.maxScale = 1.0f;
    ScopedSettings.targetMilliseconds = 16.0f;
    ScopedSettings.step = 0.1f;
    ScopedSettings.settleFrames = 2;

    Fby::Rhi::DynamicResolution ScopedController;
    ScopedController.Configure(ScopedSettings);
    FBY_CHECK(ScopedController.Scale() == 1.0f);

    // One slow frame is ignored; the second lowers the scale.
    FBY_CHECK(ScopedController.Update(20.0f) == 1.0f);
    FBY_CHECK(ScopedController.Update(20.0f) == 0.9f);
    FBY_CHECK(ScopedController.Update(20.0f) == 0.9f);
    FBY_CHECK(std::fabs(ScopedController.Update(20.0f) - 0.8f) < 1.0e-4f);

    // Fast frames raise it back.
    FBY_CHECK(std::fabs(ScopedController.Update(10.0f) - 0.8f) < 1.0e-4f);
    FBY_CHECK(std::fabs(ScopedController.Update(10.0f) - 0.9f) < 1.0e-4f);

    // Minimum clamp.
    for (int ScopedFrame = 0; ScopedFrame < 20; ++ScopedFrame)
    {
        (void)ScopedController.Update(20.0f);
    }
    FBY_CHECK(ScopedController.Scale() >= 0.5f);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "dynamic_resolution_test: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("dynamic_resolution_test: OK\n");
    return 0;
}
