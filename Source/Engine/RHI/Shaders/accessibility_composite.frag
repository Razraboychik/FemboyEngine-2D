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
// Per-pixel luminance history written by this same pass. Keeping the flash guard
// here means the scene image never needs storage-image usage, which sRGB formats
// do not have to support.
layout(set = 0, binding = 1, r8) uniform image2D uFlashHistory;

layout(push_constant) uniform AccessibilityPush
{
    // Row-major linear-light correction, packed as three vec4 columns.
    mat3 colorMatrix;
    // x = residual gain, y = correction strength, z = flags (1 CVD, 2 gain)
    vec4 safety;
    // x = threshold, y = residual gain, z = enabled, w = reserved
    vec4 flashGuard;
} a;

void main()
{
    vec4 color = texture(uScene, vUv);
    if (a.flashGuard.z > 0.5)
    {
        const ivec2 pixel = ivec2(gl_FragCoord.xy);
        const float rawLuminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        const float previous = imageLoad(uFlashHistory, pixel).r;
        if (previous > 0.0 && abs(rawLuminance - previous) > a.flashGuard.x)
        {
            color.rgb *= clamp(a.flashGuard.y, 0.0, 1.0);
        }
        imageStore(uFlashHistory, pixel, vec4(rawLuminance, 0.0, 0.0, 0.0));
    }
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
