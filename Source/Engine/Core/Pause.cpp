// FemboyEngine2D - pause policy implementation.
#include "Engine/Core/Pause.hpp"

#include "Engine/Audio/Audio.hpp"

namespace Fby::Core
{

void PauseController::ApplyAudio(AudioSystem* InAudio) noexcept
{
    if (InAudio == nullptr)
    {
        return;
    }
    InAudio->SetBusPaused(AudioBus::Sfx, MPaused && MSettings.PauseSfx);
    InAudio->SetBusPaused(AudioBus::GameSfx, MPaused && MSettings.PauseSfx);
    InAudio->SetBusPaused(AudioBus::Music, MPaused && MSettings.PauseMusic);
}

void PauseController::Apply(Clock& InClock, AudioSystem* InAudio) noexcept
{
    if (MSettings.PauseSimulation)
    {
        if (MPaused)
        {
            InClock.Pause();
        }
        else
        {
            InClock.Resume();
        }
    }
    else if (InClock.IsPaused())
    {
        InClock.Resume();
    }
    ApplyAudio(InAudio);
}

void PauseController::SetPaused(bool InPaused, Clock& InClock, AudioSystem* InAudio) noexcept
{
    MPaused = InPaused;
    Apply(InClock, InAudio);
}

float PauseController::HudDelta(float InSimulationDelta, float InRealDelta) const noexcept
{
    if (!MPaused)
    {
        return InSimulationDelta;
    }
    if (!MSettings.PauseHudAnimations)
    {
        return InRealDelta;
    }
    return 0.0f;
}

}
