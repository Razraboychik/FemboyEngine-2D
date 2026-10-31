#version 450
// FemboyEngine 2.0 - mesh fragment shader.
//
// Same atlas contract as sprites: one sampler2DArray, layer 0 is white.

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uint vLayer;

layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 texel = texture(uAtlas, vec3(vUv, float(vLayer)));
    const vec4 color = texel * vColor;
    if (color.a < 0.002)
    {
        discard;
    }
    outColor = color;
}
