// FemboyEngine2D - allocation-free scalar tween with pause, dilation and loops.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Fby::Animation
{

enum class Ease : std::uint8_t
{
    Linear = 0,
    QuadIn,
    QuadOut,
    QuadInOut,
    CubicIn,
    CubicOut,
    CubicInOut,
    SineInOut,
    BackOut,
};

[[nodiscard]] inline float ApplyEase(Ease InEase, float InT) noexcept
{
    const float ScopedT = std::clamp(InT, 0.0f, 1.0f);
    switch (InEase)
    {
        case Ease::QuadIn:
            return ScopedT * ScopedT;
        case Ease::QuadOut:
            return 1.0f - (1.0f - ScopedT) * (1.0f - ScopedT);
        case Ease::QuadInOut:
            return ScopedT < 0.5f ? 2.0f * ScopedT * ScopedT
                                  : 1.0f - 2.0f * (1.0f - ScopedT) * (1.0f - ScopedT);
        case Ease::CubicIn:
            return ScopedT * ScopedT * ScopedT;
        case Ease::CubicOut:
            return 1.0f - std::pow(1.0f - ScopedT, 3.0f);
        case Ease::CubicInOut:
            return ScopedT < 0.5f ? 4.0f * ScopedT * ScopedT * ScopedT
                                  : 1.0f - std::pow(-2.0f * ScopedT + 2.0f, 3.0f) / 2.0f;
        case Ease::SineInOut:
            return -(std::cos(3.14159265358979323846f * ScopedT) - 1.0f) / 2.0f;
        case Ease::BackOut:
        {
            constexpr float ScopedC1 = 1.70158f;
            constexpr float ScopedC3 = ScopedC1 + 1.0f;
            return 1.0f + ScopedC3 * std::pow(ScopedT - 1.0f, 3.0f) +
                   ScopedC1 * std::pow(ScopedT - 1.0f, 2.0f);
        }
        case Ease::Linear:
        default:
            return ScopedT;
    }
}

struct TweenDesc
{
    float from = 0.0f;
    float to = 1.0f;
    float durationSeconds = 1.0f;
    float delaySeconds = 0.0f;
    Ease easing = Ease::Linear;
    bool loop = false;
    bool pingPong = false;
    // When false the tween keeps running while the game is paused.
    bool affectByGamePause = true;
    // When false raw (unscaled) time is used.
    bool affectByTimeDilation = true;
};

class Tween
{
public:
    [[nodiscard]] bool Start(const TweenDesc& InDesc) noexcept
    {
        if (InDesc.durationSeconds <= 0.0f)
        {
            return false;
        }
        MDesc = InDesc;
        MElapsed = 0.0f;
        MStarted = true;
        MFinished = false;
        return true;
    }

    void Stop() noexcept
    {
        MStarted = false;
        MFinished = false;
        MElapsed = 0.0f;
    }

    [[nodiscard]] bool Update(float InRawDeltaSeconds, bool InPaused, float InTimeDilation) noexcept
    {
        if (!MStarted || MFinished)
        {
            return false;
        }
        float ScopedDelta = std::max(0.0f, InRawDeltaSeconds);
        if (InPaused && MDesc.affectByGamePause)
        {
            ScopedDelta = 0.0f;
        }
        if (MDesc.affectByTimeDilation)
        {
            ScopedDelta *= std::max(0.0f, InTimeDilation);
        }
        if (MDesc.delaySeconds > 0.0f)
        {
            MDesc.delaySeconds -= ScopedDelta;
            return true;
        }
        MElapsed += ScopedDelta;
        if (!MDesc.loop && MElapsed >= MDesc.durationSeconds)
        {
            MElapsed = MDesc.durationSeconds;
            MFinished = true;
        }
        return true;
    }

    [[nodiscard]] float Progress() const noexcept
    {
        if (!MStarted)
        {
            return 0.0f;
        }
        float ScopedT = std::clamp(MElapsed / MDesc.durationSeconds, 0.0f, 1.0f);
        if (MDesc.loop)
        {
            ScopedT = std::fmod(std::max(0.0f, MElapsed), MDesc.durationSeconds) / MDesc.durationSeconds;
        }
        if (MDesc.pingPong)
        {
            const float ScopedCycle = std::fmod(std::max(0.0f, MElapsed), MDesc.durationSeconds * 2.0f) /
                                      MDesc.durationSeconds;
            ScopedT = ScopedCycle <= 1.0f ? ScopedCycle : 2.0f - ScopedCycle;
        }
        return ScopedT;
    }

    [[nodiscard]] float Value() const noexcept
    {
        return MDesc.from + (MDesc.to - MDesc.from) * ApplyEase(MDesc.easing, Progress());
    }

    [[nodiscard]] bool Finished() const noexcept
    {
        return MFinished;
    }
    [[nodiscard]] bool Running() const noexcept
    {
        return MStarted && !MFinished;
    }
    [[nodiscard]] const TweenDesc& Desc() const noexcept
    {
        return MDesc;
    }
    void SetAffectByGamePause(bool InAffect) noexcept
    {
        MDesc.affectByGamePause = InAffect;
    }
    void SetAffectByTimeDilation(bool InAffect) noexcept
    {
        MDesc.affectByTimeDilation = InAffect;
    }

private:
    TweenDesc MDesc{};
    float MElapsed = 0.0f;
    bool MStarted = false;
    bool MFinished = false;
};

// Fixed-capacity group. Start() reuses finished slots, so a frame loop does not
// allocate and a long-running game cannot grow tween memory without a bound.
class TweenGroup
{
public:
    static constexpr std::size_t kMaxTweens = 256;

    [[nodiscard]] Tween* Start(const TweenDesc& InDesc) noexcept
    {
        for (std::size_t ScopedIndex = 0; ScopedIndex < kMaxTweens; ++ScopedIndex)
        {
            if (!MTweens[ScopedIndex].Running())
            {
                if (MTweens[ScopedIndex].Start(InDesc))
                {
                    return &MTweens[ScopedIndex];
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    void Update(float InRawDeltaSeconds, bool InPaused, float InTimeDilation) noexcept
    {
        for (Tween& ScopedTween : MTweens)
        {
            (void)ScopedTween.Update(InRawDeltaSeconds, InPaused, InTimeDilation);
        }
    }

    [[nodiscard]] std::size_t RunningCount() const noexcept
    {
        std::size_t ScopedCount = 0;
        for (const Tween& ScopedTween : MTweens)
        {
            ScopedCount += ScopedTween.Running() ? 1u : 0u;
        }
        return ScopedCount;
    }

    [[nodiscard]] Tween& At(std::size_t InIndex) noexcept
    {
        return MTweens[InIndex];
    }
    [[nodiscard]] const Tween& At(std::size_t InIndex) const noexcept
    {
        return MTweens[InIndex];
    }

private:
    std::array<Tween, kMaxTweens> MTweens{};
};

}
