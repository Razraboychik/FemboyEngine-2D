#version 450
// FemboyEngine 2.0 - CRT / vignette / bloom / chromatic composite.
//
// One fullscreen pass covers every post effect. A zero flag mask still runs it
// once; there is no second pipeline and no per-effect render target.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;

layout(push_constant) uniform PostFxBlock
{
    vec4 flagsAndCrt;
    vec4 vignetteAndBloom;
    vec4 exposureAndChromatic;
    vec4 grainAndFlash;
    vec4 colorRow0;
    vec4 colorRow1;
    vec4 colorRow2;
    // Generic camera distortion: flags, double-vision strength, stretch X/Y;
    // radial warp, chromatic split, wobble amplitude and frequency.
    vec4 distortion0;
    vec4 distortion1;
    vec4 effect0;
    vec4 effect1;
    vec4 effect2;
    vec4 effect3;
    vec4 effect4;
    vec4 palette0;
    vec4 palette1;
    vec4 palette2;
    vec4 palette3;
    vec4 palette4;
} fx;

uint flagBit(uint bit)
{
    return uint(fx.flagsAndCrt.x) & bit;
}

float hash12(vec2 value)
{
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

float luma(vec3 value)
{
    return dot(value, vec3(0.299, 0.587, 0.114));
}

vec3 sceneColor(vec2 uv)
{
    return texture(uScene, uv).rgb;
}

// FXAA: find the dominant local edge and blend along its perpendicular.
vec3 fxaa(vec2 uv)
{
    const vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    const vec3 north = sceneColor(uv + vec2(0.0, -texel.y));
    const vec3 south = sceneColor(uv + vec2(0.0, texel.y));
    const vec3 west = sceneColor(uv + vec2(-texel.x, 0.0));
    const vec3 east = sceneColor(uv + vec2(texel.x, 0.0));
    const float lumaN = luma(north);
    const float lumaS = luma(south);
    const float lumaW = luma(west);
    const float lumaE = luma(east);
    const float lumaMin = min(luma(sceneColor(uv)), min(min(lumaN, lumaS), min(lumaW, lumaE)));
    const float lumaMax = max(luma(sceneColor(uv)), max(max(lumaN, lumaS), max(lumaW, lumaE)));
    const float contrast = lumaMax - lumaMin;
    if (contrast < 0.08)
    {
        return sceneColor(uv);
    }
    const bool horizontal = abs(lumaN - lumaS) > abs(lumaW - lumaE);
    const vec2 direction = horizontal ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
    const vec3 a = sceneColor(uv - direction);
    const vec3 b = sceneColor(uv + direction);
    return (a + b + sceneColor(uv) * 2.0) * 0.25;
}

// SMAA-style edge-aware 8-tap reconstruction. It keeps the same post pass and
// avoids a separate edge/weight render target.
vec3 smaa(vec2 uv)
{
    const vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    const vec3 centre = sceneColor(uv);
    const vec3 neighbours[8] = {
        sceneColor(uv + vec2(-texel.x, -texel.y)), sceneColor(uv + vec2(0.0, -texel.y)),
        sceneColor(uv + vec2(texel.x, -texel.y)),  sceneColor(uv + vec2(-texel.x, 0.0)),
        sceneColor(uv + vec2(texel.x, 0.0)),       sceneColor(uv + vec2(-texel.x, texel.y)),
        sceneColor(uv + vec2(0.0, texel.y)),        sceneColor(uv + vec2(texel.x, texel.y))};
    const float centreLuma = luma(centre);
    vec3 sum = centre;
    float weight = 1.0;
    for (uint index = 0u; index < 8u; ++index)
    {
        const float edge = abs(luma(neighbours[index]) - centreLuma);
        const float tapWeight = exp(-edge * 18.0);
        sum += neighbours[index] * tapWeight;
        weight += tapWeight;
    }
    return sum / weight;
}

void main()
{
    vec2 uv = vUv;
    const uint flags = uint(fx.flagsAndCrt.x);
    const uint distortionFlags = uint(fx.distortion0.x);
    // Anisotropic stretch: a non-uniform UV scale is what makes a scene feel
    // "melted"; 1.0 on both axes keeps the image proportional.
    if ((distortionFlags & 2u) != 0u)
    {
        const vec2 scale = max(vec2(abs(fx.distortion0.z), abs(fx.distortion0.w)), vec2(0.05));
        uv = (uv - 0.5) / scale + 0.5;
    }
    if ((distortionFlags & 4u) != 0u)
    {
        vec2 centred = uv * 2.0 - 1.0;
        centred *= 1.0 + fx.distortion1.x * dot(centred, centred);
        uv = centred * 0.5 + 0.5;
    }
    if ((distortionFlags & 16u) != 0u)
    {
        const float phase = fx.flagsAndCrt.w * max(fx.distortion1.w, 0.0) * 6.2831853;
        uv.x += sin(uv.y * 18.0 + phase) * fx.distortion1.z;
        uv.y += cos(uv.x * 15.0 + phase * 0.73) * fx.distortion1.z * 0.6;
    }
    if ((flags & 1u) != 0u)
    {
        vec2 centred = uv * 2.0 - 1.0;
        centred *= 1.0 + fx.flagsAndCrt.y * dot(centred, centred);
        uv = centred * 0.5 + 0.5;
    }
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const vec2 centre = uv - 0.5;
    const float dynamicChromatic = (distortionFlags & 8u) != 0u ? fx.distortion1.y * 0.0025 : 0.0;
    const float aberration = (flags & 8u) != 0u ? fx.exposureAndChromatic.x * 0.0025 + dynamicChromatic
                                                 : dynamicChromatic;
    vec3 color;
    if ((flags & 128u) != 0u)
    {
        color = smaa(uv);
    }
    else if ((flags & 64u) != 0u)
    {
        color = fxaa(uv);
    }
    else
    {
        color.r = texture(uScene, uv + centre * aberration).r;
        color.g = texture(uScene, uv).g;
        color.b = texture(uScene, uv - centre * aberration).b;
    }
    if ((distortionFlags & 1u) != 0u)
    {
        const vec2 ghostOffset = vec2(fx.distortion0.y * 0.012, 0.0);
        const vec3 ghost = texture(uScene, clamp(uv + ghostOffset, vec2(0.0), vec2(1.0))).rgb;
        color = mix(color, ghost, clamp(abs(fx.distortion0.y) * 0.35, 0.0, 0.5));
    }
    if ((flags & 4u) != 0u)
    {
        color += texture(uBloom, uv).rgb * fx.vignetteAndBloom.z;
    }
    color *= fx.vignetteAndBloom.w;
    if ((flags & 2u) != 0u)
    {
        const float radius = length(centre * vec2(1.15, 1.0));
        color *= smoothstep(0.85, 0.15, radius) * fx.vignetteAndBloom.x +
                 (1.0 - fx.vignetteAndBloom.x);
    }
    if ((flags & 1u) != 0u)
    {
        const float scan = 0.5 + 0.5 * sin(uv.y * fx.grainAndFlash.y * 3.14159265);
        color *= 1.0 - fx.flagsAndCrt.z * (1.0 - scan);
    }
    if ((flags & 16u) != 0u)
    {
        const float grain = hash12(uv * fx.grainAndFlash.xy + fx.flagsAndCrt.w);
        color += (grain - 0.5) * fx.exposureAndChromatic.y;
    }
    // Accessibility: colour-vision correction (bit 8) and the flash-guard gain
    // (bit 5). The matrix and the strength come from AccessibilityKernel, so the
    // editor preview and the game frame apply the same numbers.
    if ((flags & 256u) != 0u)
    {
        const mat3 correction = mat3(fx.colorRow0.xyz, fx.colorRow1.xyz, fx.colorRow2.xyz);
        color = mix(color, correction * color, clamp(fx.exposureAndChromatic.w, 0.0, 1.0));
    }
    if ((flags & 32u) != 0u)
    {
        color *= clamp(fx.exposureAndChromatic.z, 0.0, 1.0);
    }
    const uint effectKind = uint(fx.effect0.x);
    if (effectKind != 0u)
    {
        const float effectAmount = clamp(fx.effect0.y, 0.0, 1.0);
        if (effectKind == 1u)
        {
            const float value = luma(color);
            const vec3 tinted = fx.effect1.rgb * (0.25 + 0.75 * value);
            color = mix(color, tinted, effectAmount);
            const uint pattern = uint(fx.effect0.z);
            if (pattern == 1u)
            {
                color *= 1.0 - 0.12 * effectAmount * (0.5 + 0.5 * sin(vUv.y * fx.grainAndFlash.y * 3.14159 *
                                                                        fx.effect0.w));
            }
            else if (pattern == 2u)
            {
                const vec2 grid = abs(fract(vUv * fx.grainAndFlash.xy * fx.effect0.w * 0.02) - 0.5);
                color *= 1.0 - 0.10 * effectAmount * smoothstep(0.48, 0.5, max(grid.x, grid.y));
            }
            else if (pattern == 3u)
            {
                color += (hash12(vUv * fx.grainAndFlash.xy) - 0.5) * 0.06 * effectAmount;
            }
        }
        else if (effectKind == 2u)
        {
            const vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
            vec3 blurred = color;
            if (fx.effect1.w > 0.0)
            {
                const float radius = fx.effect1.w;
                blurred = (color + sceneColor(vUv + vec2(texel.x * radius, 0.0)) +
                           sceneColor(vUv - vec2(texel.x * radius, 0.0)) +
                           sceneColor(vUv + vec2(0.0, texel.y * radius)) +
                           sceneColor(vUv - vec2(0.0, texel.y * radius))) * 0.2;
            }
            const float value = clamp(luma(blurred), 0.0, 1.0);
            const vec3 ramp0 = mix(fx.palette0.rgb, fx.palette1.rgb, clamp(value * 2.0, 0.0, 1.0));
            const vec3 ramp1 = mix(fx.palette2.rgb, fx.palette3.rgb, clamp(value * 2.0 - 1.0, 0.0, 1.0));
            const vec3 ramp = value < 0.5 ? ramp0 : mix(ramp1, fx.palette4.rgb, clamp(value * 2.0 - 2.0, 0.0, 1.0));
            color = mix(blurred, ramp, effectAmount);
        }
        else if (effectKind == 3u)
        {
            const float scaleBreath = fx.effect4.z * effectAmount;
            vec2 distortedUv = (vUv - 0.5) * (1.0 + sin(fx.flagsAndCrt.w * 1.7) * scaleBreath * 0.03) + 0.5;
            const float sway = fx.effect4.y * effectAmount;
            if (sway > 0.0)
            {
                distortedUv += vec2(sin(fx.flagsAndCrt.w * 0.6), cos(fx.flagsAndCrt.w * 0.45)) * sway * 0.008;
            }
            const float wobble = fx.effect2.z * effectAmount;
            if (wobble > 0.0)
            {
                distortedUv.x += sin(vUv.y * 18.0 + fx.flagsAndCrt.w * fx.effect2.w) * wobble * 0.012;
                distortedUv.y += cos(vUv.x * 14.0 + fx.flagsAndCrt.w * fx.effect2.w) * wobble * 0.008;
            }
            const vec3 base = sceneColor(distortedUv);
            const float ghost = clamp(fx.effect2.y * effectAmount * 0.02, 0.0, 0.08);
            const vec3 doubled = mix(base, sceneColor(distortedUv + vec2(ghost, 0.0)), clamp(fx.effect2.y, 0.0, 0.5));
            vec3 sampled = doubled;
            if (fx.effect1.w > 0.0)
            {
                const vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
                sampled = (sampled + sceneColor(distortedUv + texel) + sceneColor(distortedUv - texel)) / 3.0;
            }
            if (fx.effect3.x > 0.0)
            {
                const float chroma = fx.effect3.x * effectAmount * 0.01;
                sampled.r = sceneColor(distortedUv + vec2(chroma, 0.0)).r;
                sampled.b = sceneColor(distortedUv - vec2(chroma, 0.0)).b;
            }
            if (fx.effect3.y > 0.0)
            {
                const float radius = length((vUv - 0.5) * vec2(1.15, 1.0));
                sampled *= 1.0 - clamp(fx.effect3.y * effectAmount, 0.0, 0.8) * smoothstep(0.3, 0.9, radius);
            }
            if (fx.effect3.z > 0.0)
            {
                sampled = mix(sampled, vec3(luma(sampled)), fx.effect3.z * effectAmount);
            }
            if (fx.effect3.w > 0.0)
            {
                const vec3 trail = sceneColor(distortedUv - vec2(0.008 * fx.effect3.w, 0.0));
                sampled = mix(sampled, trail, clamp(fx.effect3.w * effectAmount * 0.35, 0.0, 0.5));
            }
            if (fx.effect4.x > 0.0)
            {
                sampled *= 1.0 + sin(fx.flagsAndCrt.w * 4.0) * fx.effect4.x * effectAmount * 0.05;
            }
            color = mix(color, sampled, effectAmount);
        }
    }
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
