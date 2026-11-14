// FemboyEngine2D - 2D environment reverb presets for DspGraph buses.
#pragma once

#include "Engine/Audio/DspGraph.hpp"

namespace Fby::Audio
{

enum class ReverbSpace : std::uint8_t
{
    Room = 0,
    Hall,
    Cave,
    Plate,
    Cathedral,
};

// All values are tuned for a wet/dry bus send; the caller still owns the return
// gain on BusFilterParams. Keep roomSize below 0.97 to avoid an endless tail.
[[nodiscard]] constexpr ReverbParams MakeReverbPreset(ReverbSpace InSpace) noexcept
{
    ReverbParams ScopedParams;
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
            ScopedParams.damping = 0.55f;
            ScopedParams.wet = 0.3f;
            break;
        case ReverbSpace::Cathedral:
            ScopedParams.roomSize = 0.94f;
            ScopedParams.damping = 0.4f;
            ScopedParams.wet = 0.48f;
            break;
        case ReverbSpace::Room:
        default:
            ScopedParams.roomSize = 0.55f;
            ScopedParams.damping = 0.45f;
            ScopedParams.wet = 0.2f;
            break;
    }
    return ScopedParams;
}

}
