// FemboyEngine2D - CRT / vignette / bloom / chromatic composite for Direct3D.
//
// Port of postfx_composite.frag. One fullscreen pass covers every post effect;
// a zero flag mask still runs it once.
cbuffer PostFxBlock : register(b0)
{
    float4 flagsAndCrt;
    float4 vignetteAndBloom;
    float4 exposureAndChromatic;
    float4 grainAndFlash;
    float4 colorRow0;
    float4 colorRow1;
    float4 colorRow2;
    float4 distortion0;
    float4 distortion1;
    float4 effect0;
    float4 effect1;
    float4 effect2;
    float4 effect3;
    float4 effect4;
    float4 palette0;
    float4 palette1;
    float4 palette2;
    float4 palette3;
    float4 palette4;
};

Texture2D SceneColor : register(t0);
Texture2D BloomColor : register(t1);
SamplerState PostFxSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

uint FlagBit(uint bit)
{
    return (uint)flagsAndCrt.x & bit;
}

float Hash12(float2 value)
{
    return frac(sin(dot(value, float2(12.9898, 78.233))) * 43758.5453);
}

float Luma(float3 value)
{
    return dot(value, float3(0.299, 0.587, 0.114));
}

float2 Texel()
{
    return 1.0 / max(grainAndFlash.xy, float2(1.0, 1.0));
}

float3 SceneSample(float2 uv)
{
    return SceneColor.Sample(PostFxSampler, uv).rgb;
}

float3 Fxaa(float2 uv)
{
    const float2 texel = Texel();
    const float3 north = SceneSample(uv + float2(0.0, -texel.y));
    const float3 south = SceneSample(uv + float2(0.0, texel.y));
    const float3 west = SceneSample(uv + float2(-texel.x, 0.0));
    const float3 east = SceneSample(uv + float2(texel.x, 0.0));
    const float lumaN = Luma(north);
    const float lumaS = Luma(south);
    const float lumaW = Luma(west);
    const float lumaE = Luma(east);
    const float3 centre = SceneSample(uv);
    const float lumaMin = min(Luma(centre), min(min(lumaN, lumaS), min(lumaW, lumaE)));
    const float lumaMax = max(Luma(centre), max(max(lumaN, lumaS), max(lumaW, lumaE)));
    const float contrast = lumaMax - lumaMin;
    if (contrast < 0.08)
    {
        return centre;
    }
    const bool horizontal = abs(lumaN - lumaS) > abs(lumaW - lumaE);
    const float2 direction = horizontal ? float2(0.0, texel.y) : float2(texel.x, 0.0);
    const float3 a = SceneSample(uv - direction);
    const float3 b = SceneSample(uv + direction);
    return (a + b + centre * 2.0) * 0.25;
}

float3 Smaa(float2 uv)
{
    const float2 texel = Texel();
    const float3 centre = SceneSample(uv);
    const float3 neighbours[8] = {
        SceneSample(uv + float2(-texel.x, -texel.y)), SceneSample(uv + float2(0.0, -texel.y)),
        SceneSample(uv + float2(texel.x, -texel.y)),  SceneSample(uv + float2(-texel.x, 0.0)),
        SceneSample(uv + float2(texel.x, 0.0)),       SceneSample(uv + float2(-texel.x, texel.y)),
        SceneSample(uv + float2(0.0, texel.y)),       SceneSample(uv + float2(texel.x, texel.y))};
    const float centreLuma = Luma(centre);
    float3 sum = centre;
    float weight = 1.0;
    for (uint index = 0u; index < 8u; ++index)
    {
        const float edge = abs(Luma(neighbours[index]) - centreLuma);
        const float tapWeight = exp(-edge * 18.0);
        sum += neighbours[index] * tapWeight;
        weight += tapWeight;
    }
    return sum / weight;
}

float4 PSMain(VSOut input) : SV_Target
{
    float2 uv = input.uv;
    const uint flags = (uint)flagsAndCrt.x;
    const uint distortionFlags = (uint)distortion0.x;
    // Anisotropic stretch: a non-uniform UV scale is what makes a scene feel
    // "melted"; 1.0 on both axes keeps the image proportional.
    if ((distortionFlags & 2u) != 0u)
    {
        const float2 scale = max(float2(abs(distortion0.z), abs(distortion0.w)), float2(0.05, 0.05));
        uv = (uv - 0.5) / scale + 0.5;
    }
    if ((distortionFlags & 4u) != 0u)
    {
        float2 centred = uv * 2.0 - 1.0;
        centred *= 1.0 + distortion1.x * dot(centred, centred);
        uv = centred * 0.5 + 0.5;
    }
    if ((distortionFlags & 16u) != 0u)
    {
        const float phase = flagsAndCrt.w * max(distortion1.w, 0.0) * 6.2831853;
        uv.x += sin(uv.y * 18.0 + phase) * distortion1.z;
        uv.y += cos(uv.x * 15.0 + phase * 0.73) * distortion1.z * 0.6;
    }
    if ((flags & 1u) != 0u)
    {
        float2 centred = uv * 2.0 - 1.0;
        centred *= 1.0 + flagsAndCrt.y * dot(centred, centred);
        uv = centred * 0.5 + 0.5;
    }
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 centre = uv - 0.5;
    const float dynamicChromatic = (distortionFlags & 8u) != 0u ? distortion1.y * 0.0025 : 0.0;
    const float aberration =
        (flags & 8u) != 0u ? exposureAndChromatic.x * 0.0025 + dynamicChromatic : dynamicChromatic;
    float3 color;
    if ((flags & 128u) != 0u)
    {
        color = Smaa(uv);
    }
    else if ((flags & 64u) != 0u)
    {
        color = Fxaa(uv);
    }
    else
    {
        color.r = SceneColor.Sample(PostFxSampler, uv + centre * aberration).r;
        color.g = SceneColor.Sample(PostFxSampler, uv).g;
        color.b = SceneColor.Sample(PostFxSampler, uv - centre * aberration).b;
    }
    if ((distortionFlags & 1u) != 0u)
    {
        const float2 ghostOffset = float2(distortion0.y * 0.012, 0.0);
        const float3 ghost = SceneColor.Sample(PostFxSampler, clamp(uv + ghostOffset, 0.0, 1.0)).rgb;
        color = lerp(color, ghost, clamp(abs(distortion0.y) * 0.35, 0.0, 0.5));
    }
    if ((flags & 4u) != 0u)
    {
        color += BloomColor.Sample(PostFxSampler, uv).rgb * vignetteAndBloom.z;
    }
    color *= vignetteAndBloom.w;
    if ((flags & 2u) != 0u)
    {
        const float radius = length(centre * float2(1.15, 1.0));
        // GLSL allows reversed smoothstep edges; HLSL does not, so the inverted
        // result is written explicitly.
        color *= (1.0 - smoothstep(0.15, 0.85, radius)) * vignetteAndBloom.x + (1.0 - vignetteAndBloom.x);
    }
    if ((flags & 1u) != 0u)
    {
        const float scan = 0.5 + 0.5 * sin(uv.y * grainAndFlash.y * 3.14159265);
        color *= 1.0 - flagsAndCrt.z * (1.0 - scan);
    }
    if ((flags & 16u) != 0u)
    {
        const float grain = Hash12(uv * grainAndFlash.xy + flagsAndCrt.w);
        color += (grain - 0.5) * exposureAndChromatic.y;
    }
    // Accessibility: colour-vision correction (bit 8) and the flash-guard gain
    // (bit 5). The matrix and the strength come from AccessibilityKernel, so the
    // editor preview and the game frame apply the same numbers.
    if ((flags & 256u) != 0u)
    {
        const float3x3 correction = float3x3(colorRow0.xyz, colorRow1.xyz, colorRow2.xyz);
        // GLSL builds the matrix from columns, so the equivalent HLSL product is
        // row-vector * matrix rather than matrix * column-vector.
        color = lerp(color, mul(color, correction), clamp(exposureAndChromatic.w, 0.0, 1.0));
    }
    if ((flags & 32u) != 0u)
    {
        color *= clamp(exposureAndChromatic.z, 0.0, 1.0);
    }
    const uint effectKind = (uint)effect0.x;
    if (effectKind != 0u)
    {
        const float effectAmount = clamp(effect0.y, 0.0, 1.0);
        if (effectKind == 1u)
        {
            const float value = Luma(color);
            const float3 tinted = effect1.rgb * (0.25 + 0.75 * value);
            color = lerp(color, tinted, effectAmount);
            const uint pattern = (uint)effect0.z;
            if (pattern == 1u)
            {
                color *= 1.0 - 0.12 * effectAmount *
                                   (0.5 + 0.5 * sin(input.uv.y * grainAndFlash.y * 3.14159 * effect0.w));
            }
            else if (pattern == 2u)
            {
                const float2 grid = abs(frac(input.uv * grainAndFlash.xy * effect0.w * 0.02) - 0.5);
                color *= 1.0 - 0.10 * effectAmount * smoothstep(0.48, 0.5, max(grid.x, grid.y));
            }
            else if (pattern == 3u)
            {
                color += (Hash12(input.uv * grainAndFlash.xy) - 0.5) * 0.06 * effectAmount;
            }
        }
        else if (effectKind == 2u)
        {
            const float2 texel = Texel();
            float3 blurred = color;
            if (effect1.w > 0.0)
            {
                const float radius = effect1.w;
                blurred = (color + SceneSample(input.uv + float2(texel.x * radius, 0.0)) +
                           SceneSample(input.uv - float2(texel.x * radius, 0.0)) +
                           SceneSample(input.uv + float2(0.0, texel.y * radius)) +
                           SceneSample(input.uv - float2(0.0, texel.y * radius))) *
                          0.2;
            }
            const float value = clamp(Luma(blurred), 0.0, 1.0);
            const float3 ramp0 = lerp(palette0.rgb, palette1.rgb, clamp(value * 2.0, 0.0, 1.0));
            const float3 ramp1 = lerp(palette2.rgb, palette3.rgb, clamp(value * 2.0 - 1.0, 0.0, 1.0));
            const float3 ramp = value < 0.5 ? ramp0 : lerp(ramp1, palette4.rgb, clamp(value * 2.0 - 2.0, 0.0, 1.0));
            color = lerp(blurred, ramp, effectAmount);
        }
        else if (effectKind == 3u)
        {
            const float scaleBreath = effect4.z * effectAmount;
            float2 distortedUv = (input.uv - 0.5) * (1.0 + sin(flagsAndCrt.w * 1.7) * scaleBreath * 0.03) + 0.5;
            const float sway = effect4.y * effectAmount;
            if (sway > 0.0)
            {
                distortedUv += float2(sin(flagsAndCrt.w * 0.6), cos(flagsAndCrt.w * 0.45)) * sway * 0.008;
            }
            const float wobble = effect2.z * effectAmount;
            if (wobble > 0.0)
            {
                distortedUv.x += sin(input.uv.y * 18.0 + flagsAndCrt.w * effect2.w) * wobble * 0.012;
                distortedUv.y += cos(input.uv.x * 14.0 + flagsAndCrt.w * effect2.w) * wobble * 0.008;
            }
            const float3 base = SceneSample(distortedUv);
            const float ghost = clamp(effect2.y * effectAmount * 0.02, 0.0, 0.08);
            const float3 doubled = lerp(base, SceneSample(distortedUv + float2(ghost, 0.0)), clamp(effect2.y, 0.0, 0.5));
            float3 sampled = doubled;
            if (effect1.w > 0.0)
            {
                const float2 texel = Texel();
                sampled = (sampled + SceneSample(distortedUv + texel) + SceneSample(distortedUv - texel)) / 3.0;
            }
            if (effect3.x > 0.0)
            {
                const float chroma = effect3.x * effectAmount * 0.01;
                sampled.r = SceneSample(distortedUv + float2(chroma, 0.0)).r;
                sampled.b = SceneSample(distortedUv - float2(chroma, 0.0)).b;
            }
            if (effect3.y > 0.0)
            {
                const float radius = length((input.uv - 0.5) * float2(1.15, 1.0));
                sampled *= 1.0 - clamp(effect3.y * effectAmount, 0.0, 0.8) * smoothstep(0.3, 0.9, radius);
            }
            if (effect3.z > 0.0)
            {
                sampled = lerp(sampled, float3(Luma(sampled), Luma(sampled), Luma(sampled)), effect3.z * effectAmount);
            }
            if (effect3.w > 0.0)
            {
                const float3 trail = SceneSample(distortedUv - float2(0.008 * effect3.w, 0.0));
                sampled = lerp(sampled, trail, clamp(effect3.w * effectAmount * 0.35, 0.0, 0.5));
            }
            if (effect4.x > 0.0)
            {
                sampled *= 1.0 + sin(flagsAndCrt.w * 4.0) * effect4.x * effectAmount * 0.05;
            }
            color = lerp(color, sampled, effectAmount);
        }
    }
    return float4(max(color, float3(0.0, 0.0, 0.0)), 1.0);
}
