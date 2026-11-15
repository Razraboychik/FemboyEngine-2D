// FemboyEngine2D - 2D reverb preset sanity checks.
#include "Engine/Audio/AudioReverb.hpp"

#include <cstdio>

int main()
{
    using namespace Fby::Audio;
    int ScopedFailures = 0;
    for (const ReverbSpace ScopedSpace :
         {ReverbSpace::Room, ReverbSpace::Hall, ReverbSpace::Cave, ReverbSpace::Plate, ReverbSpace::Cathedral})
    {
        const Fby::ReverbParams ScopedParams = MakeReverbPreset(ScopedSpace);
        if (!ScopedParams.enabled || ScopedParams.roomSize <= 0.0f || ScopedParams.roomSize >= 0.97f ||
            ScopedParams.damping < 0.0f || ScopedParams.damping > 1.0f || ScopedParams.wet <= 0.0f ||
            ScopedParams.wet > 1.0f)
        {
            std::fprintf(stderr, "reverb preset invalid\n");
            ++ScopedFailures;
        }
    }
    if (ScopedFailures != 0)
    {
        return 1;
    }
    std::printf("reverb presets: ok\n");
    return 0;
}
