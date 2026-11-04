// FemboyEngine2D - dynamic render scale controller.
#include "Engine/RHI/DynamicResolution.hpp"

#include <algorithm>

namespace Fby::Rhi
{

void DynamicResolution::Configure(const DynamicResolutionSettings& InSettings) noexcept
{
    MSettings = InSettings;
    MSettings.minScale = std::clamp(MSettings.minScale, 0.25f, 1.0f);
    MSettings.maxScale = std::clamp(MSettings.maxScale, MSettings.minScale, 1.0f);
    MSettings.targetMilliseconds = std::max(MSettings.targetMilliseconds, 1.0f);
    MSettings.step = std::clamp(MSettings.step, 0.01f, 0.25f);
    MSettings.settleFrames = std::max(MSettings.settleFrames, 1u);
    MScale = std::clamp(MScale, MSettings.minScale, MSettings.maxScale);
    MSlowFrames = 0;
    MFastFrames = 0;
}

float DynamicResolution::Update(float InFrameMilliseconds) noexcept
{
    if (InFrameMilliseconds <= 0.0f)
    {
        return MScale;
    }

    if (InFrameMilliseconds > MSettings.targetMilliseconds * 1.08f)
    {
        ++MSlowFrames;
        MFastFrames = 0;
        if (MSlowFrames >= MSettings.settleFrames)
        {
            MScale = std::max(MSettings.minScale, MScale - MSettings.step);
            MSlowFrames = 0;
        }
    }
    else if (InFrameMilliseconds < MSettings.targetMilliseconds * 0.85f)
    {
        ++MFastFrames;
        MSlowFrames = 0;
        if (MFastFrames >= MSettings.settleFrames)
        {
            MScale = std::min(MSettings.maxScale, MScale + MSettings.step);
            MFastFrames = 0;
        }
    }
    else
    {
        MSlowFrames = 0;
        MFastFrames = 0;
    }
    return MScale;
}

}
