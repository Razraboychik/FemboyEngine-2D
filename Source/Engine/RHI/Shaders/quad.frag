#version 450
// FemboyEngine 2.0 - quad fragment shader.
//
// One pipeline for sprites and solid geometry: layer 0 of the texture array is a
// white pixel, so a rectangle is a quad sampling the white texel, not a second
// pipeline and not a pipeline flush.
//
// SDF shadows run per pixel here or come from the screen-space compute mask.
// Items flagged RenderFlags::VirtualTexture resolve their world position through
// the page table and sample the bound page array with manual bilinear filtering;
// a missing page falls back to a neutral tint.

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uint vLayer;
layout(location = 3) in vec2 vWorld;
layout(location = 4) in vec2 vScreen;
layout(location = 5) flat in uint vFlags;

layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;
layout(set = 0, binding = 1) uniform sampler2D uSdf;
layout(set = 0, binding = 3) uniform sampler2D uShadow;
// Virtual texture: R32_UINT page table, the RGBA8 page array and the per-frame
// feedback images (one per frame slot, cleared by the backend each frame).
layout(set = 0, binding = 4) uniform usampler2D uVtTable;
layout(set = 0, binding = 5) uniform sampler2DArray uVtPages;
layout(set = 0, binding = 6, r32ui) uniform uimage2D uFeedback[3];

layout(set = 0, binding = 2) uniform SdfBlock
{
    vec4 fieldBounds;
    vec4 sun;
    vec4 params;
    vec4 flags;
    vec4 occluders[32];
} sdf;

layout(push_constant) uniform Camera
{
    // viewportSize.xy, zoom, padding
    vec4 viewportAndZoom;
    // center.xy, padding, padding
    vec4 centerAndPadding;
    // worldTileSize, tableOriginX, tableOriginY, enabled
    vec4 vtWorld;
    // tileSize, pageSize, tableSizeX, tableSizeY
    vec4 vtLayout;
    // Feedback image array slot, padding
    vec4 frame;
    // Sprite material clock: x = seconds, yz = viewport pixels, w reserved.
    vec4 materialParams;
} camera;

layout(location = 0) out vec4 outColor;

const uint kMaxOccluders = 16u;
const uint kRenderFlagVirtualTexture = 1u << 5u;
const uint kRenderFlagSdfText = 1u << 6u;
const uint kRenderFlagSdfOutline = 1u << 7u;
const uint kRenderFlagSdfGlow = 1u << 8u;
const uint kPageTableMissing = 0xFFFFFFFFu;

float fieldDistance(vec2 world)
{
    const vec2 uv = (world - sdf.fieldBounds.xy) / max(sdf.fieldBounds.zw, vec2(0.0001));
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
    {
        return 1.0e6;
    }
    return texture(uSdf, uv).r * sdf.flags.x;
}

float occluderDistance(vec2 world, uint count)
{
    float best = 1.0e6;
    for (uint index = 0u; index < count; ++index)
    {
        const vec4 shape = sdf.occluders[index * 2u];
        const vec4 extents = sdf.occluders[index * 2u + 1u];
        const vec2 local = world - shape.xy;
        if (extents.z > 0.5)
        {
            const vec2 q = abs(local) - extents.xy;
            best = min(best, length(max(q, 0.0)) + min(max(q.x, q.y), 0.0));
        }
        else
        {
            best = min(best, length(local) - shape.z);
        }
    }
    return best;
}

float visibility(vec2 world)
{
    const uint available = min(uint(sdf.params.y + 0.5), kMaxOccluders);
    const uint count = occluderDistance(world, available) <= 0.0 ? 0u : available;

    const vec2 direction = normalize(sdf.sun.xy);
    const float softness = max(sdf.sun.z, 0.001);
    const uint steps = uint(max(sdf.params.x, 1.0));
    float travelled = 0.5;
    float ratio = 1.0;
    for (uint index = 0u; index < steps; ++index)
    {
        const vec2 probe = world + direction * travelled;
        const float distance = min(fieldDistance(probe), occluderDistance(probe, count));
        if (distance <= 0.0)
        {
            return 0.0;
        }
        ratio = min(ratio, distance / (softness * travelled));
        travelled += max(distance * 0.8, 0.5);
        if (travelled > 4096.0)
        {
            break;
        }
    }
    return clamp(ratio, 0.0, 1.0);
}

float ambientOcclusion(vec2 world)
{
    const float radius = max(sdf.params.z, 1.0);
    const float strength = clamp(sdf.params.w, 0.0, 1.0);
    return mix(1.0, clamp(fieldDistance(world) / radius, 0.0, 1.0), strength);
}

vec4 samplePage(uint layer, ivec2 slotCell, float localU, float localV, int tileSize)
{
    const vec2 pixel = vec2(localU, localV) * float(tileSize) - 0.5;
    const ivec2 base = slotCell * tileSize;
    const ivec2 corner = ivec2(floor(pixel));
    const vec2 frac = pixel - vec2(corner);
    const ivec2 maxInTile = ivec2(tileSize - 1);
    const ivec2 p00 = base + clamp(corner, ivec2(0), maxInTile);
    const ivec2 p10 = base + clamp(corner + ivec2(1, 0), ivec2(0), maxInTile);
    const ivec2 p01 = base + clamp(corner + ivec2(0, 1), ivec2(0), maxInTile);
    const ivec2 p11 = base + clamp(corner + ivec2(1, 1), ivec2(0), maxInTile);
    const vec4 c00 = texelFetch(uVtPages, ivec3(p00, int(layer)), 0);
    const vec4 c10 = texelFetch(uVtPages, ivec3(p10, int(layer)), 0);
    const vec4 c01 = texelFetch(uVtPages, ivec3(p01, int(layer)), 0);
    const vec4 c11 = texelFetch(uVtPages, ivec3(p11, int(layer)), 0);
    const vec4 top = mix(c00, c10, frac.x);
    const vec4 bottom = mix(c01, c11, frac.x);
    return mix(top, bottom, frac.y);
}

// Mirrors PackVirtualTextureFeedback: bit 31 valid, two 15-bit signed axes.
uint packFeedbackTile(ivec2 tile)
{
    const int clampedX = clamp(tile.x, -16383, 16383);
    const int clampedY = clamp(tile.y, -16383, 16383);
    return 0x80000000u | uint(clampedX + 0x4000) | (uint(clampedY + 0x4000) << 16u);
}

void writeFeedback(vec2 world)
{
    const float tileWorld = max(camera.vtWorld.x, 0.0001);
    const ivec2 tile = ivec2(floor(world / tileWorld));
    const uint slot = uint(camera.frame.x + 0.5);
    const ivec2 size = imageSize(uFeedback[slot]);
    const vec2 viewport = max(camera.viewportAndZoom.xy, vec2(1.0));
    const ivec2 texel = clamp(ivec2(vec2(gl_FragCoord.xy) * vec2(size) / viewport), ivec2(0), size - ivec2(1));
    imageStore(uFeedback[slot], texel, uvec4(packFeedbackTile(tile)));
}

vec3 debugTint(uint layer, uint slot, uint mode)
{
    const uint key = mode == 1u ? (layer * 1024u + slot) : slot;
    const float r = fract(sin(float(key) * 12.9898) * 43758.5453);
    const float g = fract(sin(float(key) * 78.233) * 12345.6789);
    const float b = fract(sin(float(key) * 37.719) * 23421.631);
    return vec3(0.25 + 0.75 * r, 0.25 + 0.75 * g, 0.25 + 0.75 * b);
}

vec4 sampleVirtualTexture(vec2 world)
{
    const uint debugMode = uint(camera.frame.y + 0.5);
    const float tileWorld = max(camera.vtWorld.x, 0.0001);
    const vec2 tileUv = world / tileWorld;
    const ivec2 tile = ivec2(floor(tileUv));
    const ivec2 origin = ivec2(camera.vtWorld.y, camera.vtWorld.z);
    const ivec2 cell = tile - origin;
    const ivec2 tableSize = textureSize(uVtTable, 0);
    if (cell.x < 0 || cell.y < 0 || cell.x >= tableSize.x || cell.y >= tableSize.y)
    {
        return debugMode > 0u ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.5, 0.5, 0.5, 1.0);
    }
    const uint entry = texelFetch(uVtTable, cell, 0).r;
    if (entry == kPageTableMissing || entry > 0x00FFFFFFu)
    {
        return debugMode > 0u ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.5, 0.5, 0.5, 1.0);
    }
    const int tileSize = int(max(camera.vtLayout.x, 1.0));
    const int pageSize = int(max(camera.vtLayout.y, 1.0));
    const int pageAxis = max(pageSize / tileSize, 1);
    const uint layer = (entry >> 16u) & 0xFFu;
    const uint slot = entry & 0xFFFFu;
    const ivec2 slotCell = ivec2(int(slot) % pageAxis, int(slot) / pageAxis);
    if (slotCell.x >= pageAxis || slotCell.y >= pageAxis)
    {
        return debugMode > 0u ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.5, 0.5, 0.5, 1.0);
    }
    if (debugMode > 0u)
    {
        return vec4(debugTint(layer, slot, debugMode), 1.0);
    }
    const vec2 local = fract(tileUv);
    return samplePage(layer, slotCell, local.x, local.y, tileSize);
}

void main()
{
    vec4 color;
    if ((vFlags & kRenderFlagSdfText) != 0u)
    {
        const float distance = texture(uAtlas, vec3(vUv, float(vLayer))).r;
        const float antialias = max(fwidth(distance), 0.0005);
        const float effectWidth = float((vFlags >> 16u) & 0xFFu) / 255.0;
        float alpha;
        if ((vFlags & kRenderFlagSdfOutline) != 0u)
        {
            const float outer = 0.5 + max(effectWidth, antialias);
            alpha = smoothstep(0.5, 0.5 + antialias, distance) -
                    smoothstep(outer, outer + antialias, distance);
        }
        else if ((vFlags & kRenderFlagSdfGlow) != 0u)
        {
            alpha = 1.0 - smoothstep(0.5, 0.5 + max(effectWidth, antialias), distance);
        }
        else
        {
            alpha = smoothstep(0.5 - antialias, 0.5 + antialias, distance);
        }
        color = vec4(vColor.rgb, vColor.a * alpha);
    }
    else if ((vFlags & (kRenderFlagSdfOutline | kRenderFlagSdfGlow)) != 0u)
    {
        const float effectWidth = float((vFlags >> 16u) & 0xFFu) / 255.0;
        const vec2 texel = max(fwidth(vUv), vec2(0.00001));
        const float radius = 1.0 + effectWidth * 8.0;
        float mask = texture(uAtlas, vec3(vUv, float(vLayer))).a;
        for (uint index = 0u; index < 8u; ++index)
        {
            const float angle = float(index) * 0.7853981634;
            const vec2 offset = vec2(cos(angle), sin(angle)) * texel * radius;
            mask = max(mask, texture(uAtlas, vec3(vUv + offset, float(vLayer))).a);
        }
        color = vec4(vColor.rgb, vColor.a * mask);
    }
    else if ((vFlags & kRenderFlagVirtualTexture) != 0u && camera.vtWorld.w > 0.5)
    {
        color = vec4(sampleVirtualTexture(vWorld).rgb * vColor.rgb, vColor.a);
        writeFeedback(vWorld);
    }
    else
    {
        color = texture(uAtlas, vec3(vUv, float(vLayer))) * vColor;
    }

    // Alpha-tested sprites keep the depth of the batch irrelevant and avoid
    // blend-order artefacts on fully transparent texels.
    if (color.a < 0.002)
    {
        discard;
    }

    if (sdf.flags.y > 0.5)
    {
        float shadow = 1.0;
        if (sdf.flags.z > 0.5)
        {
            shadow = texture(uShadow, vScreen).r;
        }
        else
        {
            shadow = mix(sdf.sun.w, 1.0, visibility(vWorld));
        }
        color.rgb *= shadow * ambientOcclusion(vWorld);
    }
    outColor = color;
}
