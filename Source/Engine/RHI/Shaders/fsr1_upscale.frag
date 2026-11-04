#version 460
// FemboyEngine2D - spatial edge-aware upscale with a sharpening pass.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSource;
layout(push_constant) uniform UpscaleConstants
{
    vec4 sourceSize; // width, height, 1/width, 1/height
    vec4 params;     // sharpen, mode, 0, 0
} upsample;
layout(location = 0) out vec4 outColor;

vec3 Cubic(sampler2D tex, vec2 uv, vec2 texel)
{
    const vec2 ScopedBase = floor(uv / texel) * texel + texel * 0.5;
    vec3 ScopedResult = vec3(0.0);
    float ScopedWeight = 0.0;
    for (int y = -1; y <= 2; ++y)
    {
        for (int x = -1; x <= 2; ++x)
        {
            const vec2 ScopedOffset = vec2(float(x), float(y)) * texel;
            const float ScopedDistance = abs(uv.x - (ScopedBase.x + ScopedOffset.x)) / texel.x +
                                         abs(uv.y - (ScopedBase.y + ScopedOffset.y)) / texel.y;
            const float ScopedTapWeight = max(1.0 - ScopedDistance * 0.5, 0.0);
            ScopedResult += texture(tex, ScopedBase + ScopedOffset).rgb * ScopedTapWeight;
            ScopedWeight += ScopedTapWeight;
        }
    }
    return ScopedWeight > 0.0 ? ScopedResult / ScopedWeight : texture(tex, uv).rgb;
}

void main()
{
    const vec2 ScopedTexel = upsample.sourceSize.zw;
    vec3 ScopedColor = Cubic(uSource, vUv, ScopedTexel);

    // Prefer the diagonal with the smaller luminance spread. This keeps edges
    // from being softened across the dominant direction.
    const vec3 ScopedNw = texture(uSource, vUv + vec2(-ScopedTexel.x, -ScopedTexel.y)).rgb;
    const vec3 ScopedNe = texture(uSource, vUv + vec2(ScopedTexel.x, -ScopedTexel.y)).rgb;
    const vec3 ScopedSw = texture(uSource, vUv + vec2(-ScopedTexel.x, ScopedTexel.y)).rgb;
    const vec3 ScopedSe = texture(uSource, vUv + vec2(ScopedTexel.x, ScopedTexel.y)).rgb;
    const vec3 ScopedLumaWeights = vec3(0.299, 0.587, 0.114);
    const float ScopedDiagA = abs(dot(ScopedNw - ScopedSe, ScopedLumaWeights));
    const float ScopedDiagB = abs(dot(ScopedNe - ScopedSw, ScopedLumaWeights));
    const float ScopedEdge = min(ScopedDiagA, ScopedDiagB);
    const float ScopedBlend = clamp(ScopedEdge * 1.5, 0.0, 0.5);
    const vec3 ScopedDirectional = ScopedDiagA < ScopedDiagB ? (ScopedNw + ScopedSe) * 0.5
                                                             : (ScopedNe + ScopedSw) * 0.5;
    ScopedColor = mix(ScopedColor, ScopedDirectional, ScopedBlend);
    if (upsample.params.x > 0.0)
    {
        const vec3 ScopedNeighbours =
            texture(uSource, vUv + vec2(ScopedTexel.x, 0.0)).rgb +
            texture(uSource, vUv - vec2(ScopedTexel.x, 0.0)).rgb +
            texture(uSource, vUv + vec2(0.0, ScopedTexel.y)).rgb +
            texture(uSource, vUv - vec2(0.0, ScopedTexel.y)).rgb;
        const vec3 ScopedAverage = ScopedNeighbours * 0.25;
        const vec3 ScopedContrast = abs(ScopedColor - ScopedAverage);
        const float ScopedLuma = dot(ScopedContrast, vec3(0.299, 0.587, 0.114));
        const float ScopedAdaptive = upsample.params.x * (1.0 - clamp(ScopedLuma, 0.0, 1.0));
        ScopedColor = clamp(ScopedColor + (ScopedColor - ScopedAverage) * ScopedAdaptive, 0.0, 1.0);
    }
    outColor = vec4(ScopedColor, texture(uSource, vUv).a);
}
