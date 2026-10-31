#version 450
// FemboyEngine 2.0 - accessibility composite.
//
// Fullscreen pass between the scene and the presented image. It applies the two
// terms AccessibilityKernel::ApplyPostFx() describes: a colour-vision correction
// matrix and the flash-guard residual gain. The matrix travels in the push block,
// so the engine has one source of truth for the correction (Accessibility.cpp).

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform AccessibilityPush
{
    // Row-major linear-light correction, packed as three vec4 columns.
    mat3 colorMatrix;
    // x = residual gain, y = correction strength, z = flags (1 CVD, 2 gain)
    vec4 safety;
} a;

void main()
{
    vec4 color = texture(uScene, vUv);
    const uint flags = uint(a.safety.z + 0.5);
    if ((flags & 1u) != 0u)
    {
        color.rgb = mix(color.rgb, a.colorMatrix * color.rgb, clamp(a.safety.y, 0.0, 1.0));
    }
    if ((flags & 2u) != 0u)
    {
        color.rgb *= clamp(a.safety.x, 0.0, 1.0);
    }
    outColor = vec4(color.rgb, 1.0);
}
