#version 450
// FemboyEngine 2.0 - plant fragment with wind-driven tint variation.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vTint;
layout(location = 0) out vec4 outColor;

void main()
{
    const float edge = smoothstep(0.0, 0.15, vUv.x) * smoothstep(1.0, 0.85, vUv.x);
    outColor = vec4(vTint.rgb * (0.85 + 0.15 * vUv.y), vTint.a * edge);
}
