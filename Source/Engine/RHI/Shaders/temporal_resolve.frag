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
    const vec4 ScopedMotion = texture(uMotion, vUv);
    const vec2 ScopedDelta = (ScopedMotion.rg * 2.0 - 1.0) * resolve.motionScale;
    const vec4 ScopedCurrent = texture(uCurrent, vUv);
    const vec4 ScopedHistory = texture(uHistory, vUv - ScopedDelta * resolve.texel);
    const float ScopedReactive = ScopedMotion.b;
    const float ScopedDepth = texture(uDepth, vUv).r;
    const float ScopedPreviousDepth = texture(uHistoryDepth, vUv - ScopedDelta * resolve.texel).r;
    const float ScopedDisocclusion = abs(ScopedDepth - ScopedPreviousDepth) > 0.01 ? 0.0 : 1.0;
    const float ScopedConfidence =
        (1.0 - clamp(length(ScopedDelta) * 0.5, 0.0, 1.0)) * ScopedDisocclusion;
    const float ScopedWeight =
        resolve.resetHistory > 0.5 ? 0.0 : resolve.historyWeight * ScopedConfidence * (1.0 - ScopedReactive);
    outColor = mix(ScopedCurrent, ScopedHistory, ScopedWeight);
}
