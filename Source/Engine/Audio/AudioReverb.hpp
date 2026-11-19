// FemboyEngine2D - 2D environment reverb presets for DspGraph buses.
#pragma once

#include "Engine/Audio/DspGraph.hpp"

#include <cstdint>

namespace Fby::Audio
{

enum class ReverbSpace : std::uint8_t
{
    Room = 0,
    Hall,
    Cave,
    Plate,
    Cathedral,
    Off,
};

// `wet` is a linear additive return gain, not a dry/wet mix ratio; the dry bus is
// never attenuated. Return gain is owned by AudioZoneSystem on zoned buses. The
// engine clamps roomSize to [0,1] and scales comb feedback internally.
[[nodiscard]] constexpr ReverbParams MakeReverbPreset(ReverbSpace InSpace) noexcept
{
    ReverbParams ScopedParams;
    if (InSpace == ReverbSpace::Off)
    {
        return ScopedParams;
    }
    ScopedParams.enabled = true;
    switch (InSpace)
    {
        case ReverbSpace::Hall:
            ScopedParams.roomSize = 0.82f;
            ScopedParams.damping = 0.28f;
            ScopedParams.wet = 0.34f;
            break;
        case ReverbSpace::Cave:
            ScopedParams.roomSize = 0.9f;
            ScopedParams.damping = 0.18f;
            ScopedParams.wet = 0.42f;
            break;
        case ReverbSpace::Plate:
            ScopedParams.roomSize = 0.74f;
            ScopedParams.damping = 0.18f;
            ScopedParams.wet = 0.3f;
            break;
        case ReverbSpace::Cathedral:
            ScopedParams.roomSize = 0.94f;
            ScopedParams.damping = 0.4f;
            ScopedParams.wet = 0.48f;
            break;
        case ReverbSpace::Room:
            ScopedParams.roomSize = 0.55f;
            ScopedParams.damping = 0.45f;
            ScopedParams.wet = 0.2f;
            break;
        case ReverbSpace::Off:
        default:
            return ReverbParams{};
    }
    return ScopedParams;
}

}
