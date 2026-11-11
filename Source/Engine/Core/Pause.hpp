// FemboyEngine2D - pause policy shared by the runtime and the editor.
#pragma once

#include "Engine/Core/Time.hpp"

#include <cstdint>

namespace Fby
{
class AudioSystem;
enum class AudioBus : std::uint8_t;
}

namespace Fby::Core
{

// Each lane can be paused independently. Music intentionally defaults to
// continuing: a menu or a cut-scene pause often keeps the soundtrack running.
struct PauseSettings
{
    bool PauseSimulation = true;
    bool PauseSfx = true;
    bool PauseMusic = false;
    bool PauseHudAnimations = true;
};

class PauseController
{
public:
    [[nodiscard]] bool Paused() const noexcept
    {
        return MPaused;
    }

    [[nodiscard]] const PauseSettings& Settings() const noexcept
    {
        return MSettings;
    }

    [[nodiscard]] PauseSettings& Settings() noexcept
    {
        return MSettings;
    }

    // Applies the current settings to the clock and the audio buses. Safe to
    // call every frame; only changes are pushed to the sub-systems.
    void Apply(Clock& InClock, AudioSystem* InAudio) noexcept;

    void SetPaused(bool InPaused, Clock& InClock, AudioSystem* InAudio) noexcept;

    [[nodiscard]] bool HudAnimationsPaused() const noexcept
    {
        return MPaused && MSettings.PauseHudAnimations;
    }

    // The delta HUD animation should integrate. When the game is paused but HUD
    // animation is allowed, it uses the real (unpaused) frame delta; otherwise
    // it falls back to the simulation delta.
    [[nodiscard]] float HudDelta(float InSimulationDelta, float InRealDelta) const noexcept;

private:
    void ApplyAudio(AudioSystem* InAudio) noexcept;

    PauseSettings MSettings{};
    bool MPaused = false;
};

}
