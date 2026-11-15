#version 460
// FemboyEngine2D - FSR2-compatible temporal resolve contract.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uCurrent;
layout(set = 0, binding = 1) uniform sampler2D uHistory;
layout(set = 0, binding = 2) uniform sampler2D uMotion;
layout(set = 0, binding = 3) uniform sampler2D uDepth;
layout(set = 0, binding = 4) uniform sampler2D uHistoryDepth;
layout(push_constant) uniform ResolveConstants
{
    vec2 texel;
    float historyWeight;
    float motionScale;
    float resetHistory;
    float padding[3];
} resolve;
layout(location = 0) out vec4 outColor;

void main()
{
    // The RG16F motion target stores the signed normalized UV delta
    // (current - previous); history is sampled by subtracting it.
    const vec4 ScopedMotion = texture(uMotion, vUv);
    const vec2 ScopedDelta = ScopedMotion.rg * resolve.motionScale;
    const vec2 ScopedHistoryUv = vUv - ScopedDelta;
    const vec4 ScopedCurrent = texture(uCurrent, vUv);
    const vec4 ScopedHistory = texture(uHistory, ScopedHistoryUv);
    // RG16F has no reactive channel; motion confidence and depth disocclusion
    // cover the history rejection that the reactive mask used to provide.
    const float ScopedDepth = texture(uDepth, vUv).r;
    const float ScopedPreviousDepth = texture(uHistoryDepth, ScopedHistoryUv).r;
    const float ScopedDisocclusion = abs(ScopedDepth - ScopedPreviousDepth) > 0.01 ? 0.0 : 1.0;
    const float ScopedConfidence = (1.0 - clamp(length(ScopedDelta) * 8.0, 0.0, 1.0)) * ScopedDisocclusion;
    const float ScopedWeight = resolve.resetHistory > 0.5 ? 0.0 : resolve.historyWeight * ScopedConfidence;
    outColor = mix(ScopedCurrent, ScopedHistory, ScopedWeight);
}
