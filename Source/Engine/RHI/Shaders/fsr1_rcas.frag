#version 460
// FemboyEngine2D - contrast-adaptive sharpening for the FSR1 path.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSource;
layout(push_constant) uniform RcasConstants
{
    vec2 texel;
    float sharpness;
    float padding;
} rcas;
layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 ScopedCenter = texture(uSource, vUv).rgb;
    const vec3 ScopedNorth = texture(uSource, vUv + vec2(0.0, rcas.texel.y)).rgb;
    const vec3 ScopedSouth = texture(uSource, vUv - vec2(0.0, rcas.texel.y)).rgb;
    const vec3 ScopedEast = texture(uSource, vUv + vec2(rcas.texel.x, 0.0)).rgb;
    const vec3 ScopedWest = texture(uSource, vUv - vec2(rcas.texel.x, 0.0)).rgb;
    const vec3 ScopedNeighbours = (ScopedNorth + ScopedSouth + ScopedEast + ScopedWest) * 0.25;
    const vec3 ScopedContrast = abs(ScopedCenter - ScopedNeighbours);
    const float ScopedWeight = rcas.sharpness * (1.0 - clamp(dot(ScopedContrast, vec3(0.299, 0.587, 0.114)), 0.0, 1.0));
    outColor = vec4(clamp(ScopedCenter + (ScopedCenter - ScopedNeighbours) * ScopedWeight, 0.0, 1.0),
                    texture(uSource, vUv).a);
}
