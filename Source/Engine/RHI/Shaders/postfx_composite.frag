#version 450
// FemboyEngine 2.0 - CRT / vignette / bloom / chromatic composite.
//
// One fullscreen pass covers every post effect. A zero flag mask still runs it
// once; there is no second pipeline and no per-effect render target.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;

layout(set = 0, binding = 2) uniform PostFxBlock
{
    vec4 flagsAndCrt;
    vec4 vignetteAndBloom;
    vec4 exposureAndChromatic;
    vec4 grainAndFlash;
    vec4 colorRow0;
    vec4 colorRow1;
    vec4 colorRow2;
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
    const float aberration = (flags & 8u) != 0u ? fx.exposureAndChromatic.x * 0.0025 : 0.0;
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
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
