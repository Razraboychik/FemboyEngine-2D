#version 450
// FemboyEngine 2.0 - procedural splash quad.
//
// A splash is not a texture: the fragment stage evaluates a ring SDF, so the
// ripple stays crisp and the atlas never grows with the particle count.

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main()
{
    const vec2 p = vUv * 2.0 - 1.0;
    const float ring = abs(length(p) - 0.62) - 0.14;
    const float mask = 1.0 - smoothstep(-0.09, 0.09, ring);
    const float core = 1.0 - smoothstep(0.0, 0.35, length(p));
    outColor = vec4(vColor.rgb, vColor.a * max(mask, core) * 0.8);
}
