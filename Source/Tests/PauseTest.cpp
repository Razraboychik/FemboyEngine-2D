// FemboyEngine2D - pause policy contract.
#include "Engine/Core/Pause.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    Fby::Core::Clock ScopedClock;
    ScopedClock.Tick(0);
    ScopedClock.Tick(10'000'000);

    Fby::Core::PauseController ScopedPause;
    ScopedPause.Settings().PauseSimulation = true;
    ScopedPause.Settings().PauseSfx = true;
    ScopedPause.Settings().PauseMusic = false;
    ScopedPause.Settings().PauseHudAnimations = true;

    ScopedPause.SetPaused(true, ScopedClock, nullptr);
    Check(ScopedPause.Paused(), "pause flag is set");
    Check(ScopedClock.IsPaused(), "simulation clock is paused");
    Check(ScopedClock.Delta() == 0.0, "paused delta is zero");
    Check(ScopedPause.HudDelta(0.016f, 0.033f) == 0.0f, "hud animation stops by default");

    ScopedPause.Settings().PauseHudAnimations = false;
    Check(ScopedPause.HudDelta(0.0f, 0.033f) == 0.033f, "hud can continue on real time while paused");
    Check(ScopedPause.HudAnimationsPaused() == false, "hud pause flag follows the setting");

    ScopedPause.SetPaused(false, ScopedClock, nullptr);
    Check(!ScopedPause.Paused(), "pause flag clears");
    Check(!ScopedClock.IsPaused(), "simulation clock resumes");
    Check(ScopedPause.HudDelta(0.016f, 0.033f) == 0.016f, "unpaused hud uses the simulation delta");

    if (GFailures == 0)
    {
        std::printf("pause: OK\n");
        return 0;
    }
    std::fprintf(stderr, "pause: %d failure(s)\n", GFailures);
    return 1;
}
