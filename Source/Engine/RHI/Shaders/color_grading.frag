#version 450
// FemboyEngine 2.0 - LUT colour grading for the final composite.
//
// The LUT is a 2D strip of N x N*N texels (the common 32x1024 layout). A 3D LUT
// uses the same slice math; the graph binds the view, the shader only samples.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uLut;

layout(set = 0, binding = 2) uniform ColorGradingBlock
{
    vec4 params0; // lut size, intensity, saturation, contrast
    vec4 params1; // brightness, lut id low, lut id high, flags
} grading;

vec3 SampleLut(vec3 color)
{
    const float size = max(grading.params0.x, 2.0);
    const float maxIndex = size - 1.0;
    const vec3 clamped = clamp(color, 0.0, 1.0);
    const float slice = floor(clamped.b * maxIndex + 0.5);
    const vec2 uv = vec2((clamped.r * maxIndex + 0.5) / size,
                         (slice * size + clamped.g * maxIndex + 0.5) / (size * size));
    return texture(uLut, uv).rgb;
}

void main()
{
    vec4 scene = texture(uScene, vUv);
    vec3 graded = SampleLut(scene.rgb);
    const float luminance = dot(graded, vec3(0.2126, 0.7152, 0.0722));
    graded = mix(vec3(luminance), graded, grading.params0.z);
    graded = (graded - 0.5) * grading.params0.w + 0.5 + grading.params1.x;
    scene.rgb = mix(scene.rgb, graded, clamp(grading.params0.y, 0.0, 1.0));
    outColor = scene;
}
