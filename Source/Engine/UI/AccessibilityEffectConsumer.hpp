// FemboyEngine2D - maps accessibility effect resources onto RHI post-FX settings.
#pragma once

#include "Engine/RHI/PostProcess.hpp"
#include "Engine/UI/AccessibilityEffects.hpp"

namespace Fby::Ui
{
// Consumes one registered effect profile. The caller owns the frame packet and
// decides which surface/profile is active; this function only translates data.
void ApplyAccessibilityEffect(const AccessibilityEffectProfile& InEffect, Rhi::PostFxSettings& InFrameSettings) noexcept;
}
