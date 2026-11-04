#version 460
// FemboyEngine2D - weighted blended OIT resolve.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uAccumulation;
layout(set = 0, binding = 1) uniform sampler2D uRevealage;
layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 accumulation = texture(uAccumulation, vUv);
    const float revealage = texture(uRevealage, vUv).r;
    const vec3 color = accumulation.rgb / max(accumulation.a, 1.0e-4);
    const float alpha = clamp(1.0 - revealage, 0.0, 1.0);
    outColor = vec4(color, alpha);
}
