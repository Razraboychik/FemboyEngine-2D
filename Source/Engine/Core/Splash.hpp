// FemboyEngine2D - launch splash timing.
#pragma once

#include <algorithm>

namespace Fby
{

// Fade in 0.5 s, hold 2 s, fade out 1 s. Any click or key skips immediately.
class SplashClock
{
public:
    static constexpr float kFadeIn = 0.5f;
    static constexpr float kHold = 2.0f;
    static constexpr float kFadeOut = 1.0f;
    static constexpr float kTotal = kFadeIn + kHold + kFadeOut;

    explicit SplashClock(bool InEnabled = true) noexcept : MEnabled(InEnabled) {}

    void Tick(float InDeltaSeconds) noexcept
    {
        if (!MEnabled || MDone)
        {
            return;
        }
        MElapsed += std::max(InDeltaSeconds, 0.0f);
        if (MElapsed >= kTotal)
        {
            MDone = true;
        }
    }

    void Skip() noexcept
    {
        MDone = true;
    }

    [[nodiscard]] bool Enabled() const noexcept
    {
        return MEnabled;
    }

    [[nodiscard]] bool Done() const noexcept
    {
        return !MEnabled || MDone;
    }

    [[nodiscard]] float Opacity() const noexcept
    {
        if (Done())
        {
            return 0.0f;
        }
        if (MElapsed < kFadeIn)
        {
            return MElapsed / kFadeIn;
        }
        if (MElapsed < kFadeIn + kHold)
        {
            return 1.0f;
        }
        return 1.0f - (MElapsed - kFadeIn - kHold) / kFadeOut;
    }

private:
    bool MEnabled = true;
    bool MDone = false;
    float MElapsed = 0.0f;
};

}
