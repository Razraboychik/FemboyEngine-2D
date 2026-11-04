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
// One layer per in-flight view slot. The push block selects the layer.
layout(set = 0, binding = 3) uniform sampler2DArray uShadow;
// Virtual texture: R32_UINT page table, the RGBA8 page array and the per-frame
// feedback images (one per frame slot, cleared by the backend each frame).
layout(set = 0, binding = 4) uniform usampler2D uVtTable;
layout(set = 0, binding = 5) uniform sampler2DArray uVtPages;
layout(set = 0, binding = 6, r32ui) uniform uimage2D uFeedback[3];

layout(set = 0, binding = 2) uniform SdfBlock
{
    // The fragment path shares the buffer with sdf_shadow.comp, so the struct must
    // keep the same std430 layout even though the fragment shader reads only
    // fieldBounds and the occluders.
    vec4 camera;        // centre.xy, zoom, padding
    vec4 viewport;      // width, height, 0, 0
    vec4 fieldBounds;   // origin.xy, extent.xy
    vec4 sun;
    vec4 params;
    vec4 flags;
    vec4 history;
    vec4 historyCamera;
    vec4 fieldGrid;      // origin.xy, cell size.xy
    vec4 brickMeta;      // brick size, columns, rows, atlas columns
    vec4 occluders[32];
} sdf;

// Sparse brick table: eight 32-bit words per brick. Word 0 is the tile index,
// words 1/2 are min/max distance as floats, word 5 is the state in the low bits.
layout(std430, set = 0, binding = 7) readonly buffer SdfBrickTable
{
    uint words[];
} brickTable;

layout(set = 0, binding = 8) uniform sampler2D uSdfAtlas;

struct GpuLight
{
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
    vec4 directionAndCone;
    uint type;
    uint flags;
    uint shadowIndex;
    uint reserved;
};

struct ClusterHeader
{
    uint offset;
    uint count;
    uint overflowCount;
    uint flags;
};

layout(set = 1, binding = 0) readonly buffer ClusterLights
{
    GpuLight values[];
} clusterLights;

layout(set = 1, binding = 1) readonly buffer ClusterHeaders
{
    ClusterHeader values[];
} clusterHeaders;

layout(set = 1, binding = 2) readonly buffer ClusterIndices
{
    uint values[];
} clusterIndices;

struct GpuGiProbe
{
    vec4 positionAndRadius;
    vec4 radiance;
    vec4 occlusion;
};

layout(set = 1, binding = 3) readonly buffer GiProbes
{
    GpuGiProbe values[];
} giProbes;

layout(set = 1, binding = 4) uniform GiVolume
{
    vec4 originAndCell;
    vec4 extent;
} giVolume;

vec3 SampleGi(vec2 InWorld, out float OutOcclusion)
{
    OutOcclusion = 1.0;
    if (giVolume.extent.x <= 0.0 || giVolume.extent.y <= 0.0 || giVolume.extent.w <= 0.0)
    {
        return vec3(0.0);
    }
    const vec2 coord = (InWorld - giVolume.originAndCell.xy) / max(giVolume.originAndCell.zw, vec2(0.0001));
    if (coord.x < 0.0 || coord.y < 0.0 || coord.x >= giVolume.extent.x || coord.y >= giVolume.extent.y)
    {
        return vec3(0.0);
    }
    const vec2 sampleCoord = coord - 0.5;
    const ivec2 base = ivec2(floor(sampleCoord));
    const vec2 fraction = sampleCoord - vec2(base);
    const ivec2 maxCoord = ivec2(giVolume.extent.xy) - 1;
    vec3 radiance = vec3(0.0);
    float occlusion = 0.0;
    for (int offsetY = 0; offsetY <= 1; ++offsetY)
    {
        for (int offsetX = 0; offsetX <= 1; ++offsetX)
        {
            const ivec2 sampleCoordInt = clamp(base + ivec2(offsetX, offsetY), ivec2(0), maxCoord);
            const uint index = uint(sampleCoordInt.y) * uint(giVolume.extent.x) + uint(sampleCoordInt.x);
            const float weight = (offsetX == 0 ? 1.0 - fraction.x : fraction.x) *
                                 (offsetY == 0 ? 1.0 - fraction.y : fraction.y);
            radiance += giProbes.values[index].radiance.rgb * weight;
            occlusion += giProbes.values[index].occlusion.x * weight;
        }
    }
    OutOcclusion = occlusion;
    return radiance;
}

vec3 ClusterLighting(vec2 InScreen, vec2 InViewport)
{
    const vec2 viewport = InViewport;
    const vec2 pixel = InScreen * viewport;
    const ivec2 tile = ivec2(pixel / 16.0);
    const uint tilesX = uint(ceil(viewport.x / 16.0));
    const uint tilesY = uint(ceil(viewport.y / 16.0));
    if (tile.x < 0 || tile.y < 0 || uint(tile.x) >= tilesX || uint(tile.y) >= tilesY)
    {
        return vec3(0.0);
    }
    const uint cluster = uint(tile.y) * tilesX + uint(tile.x);
    const ClusterHeader header = clusterHeaders.values[cluster];
    vec3 result = vec3(0.0);
    for (uint index = 0u; index < header.count; ++index)
    {
        const uint lightIndex = clusterIndices.values[header.offset + index];
        const GpuLight light = clusterLights.values[lightIndex];
        const float radius = max(light.positionAndRadius.z, 1.0);
        const vec2 toPixel = pixel - light.positionAndRadius.xy;
        const float distance = length(toPixel);
        const float attenuation = clamp(1.0 - distance / radius, 0.0, 1.0);
        float cone = 1.0;
        if (light.directionAndCone.z > -0.5 && light.directionAndCone.w > -0.5 && distance > 0.0001)
        {
            const vec2 direction = normalize(light.directionAndCone.xy);
            const float cosine = dot(direction, toPixel / distance);
            cone = smoothstep(light.directionAndCone.z, light.directionAndCone.w, cosine);
        }
        result += light.colorAndIntensity.rgb * (light.colorAndIntensity.a * attenuation * cone);
    }
    return result;
}

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
const uint kRenderFlagDepthWrite = 1u << 9u;
const uint kPageTableMissing = 0xFFFFFFFFu;

float fieldDistance(vec2 world)
{
    const vec2 uv = (world - sdf.fieldBounds.xy) / max(sdf.fieldBounds.zw, vec2(0.0001));
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
    {
        return 1.0e6;
    }
    const float atlasColumns = sdf.brickMeta.w;
    if (atlasColumns > 0.5)
    {
        // Sparse path: world -> cell -> brick -> table -> atlas tile.
        const float brickSize = max(sdf.brickMeta.x, 1.0);
        const vec2 cell = (world - sdf.fieldGrid.xy) / max(sdf.fieldGrid.zw, vec2(0.0001));
        const ivec2 brick = ivec2(floor(cell / brickSize));
        const uint columns = uint(sdf.brickMeta.y);
        const uint rows = uint(sdf.brickMeta.z);
        if (brick.x < 0 || brick.y < 0 || uint(brick.x) >= columns || uint(brick.y) >= rows)
        {
            return 1.0e6;
        }
        const uint base = (uint(brick.y) * columns + uint(brick.x)) * 8u;
        const float minDistance = uintBitsToFloat(brickTable.words[base + 1u]);
        const float maxDistance = uintBitsToFloat(brickTable.words[base + 2u]);
        const uint state = brickTable.words[base + 5u] & 3u;
        if (state == 0u)
        {
            return max(minDistance, 0.0);
        }
        if (state == 1u)
        {
            return min(maxDistance, 0.0);
        }
        const uint tile = brickTable.words[base + 0u];
        const vec2 local = (cell - vec2(brick) * brickSize) / brickSize;
        const vec2 tileOrigin = vec2(float(tile % uint(atlasColumns)), float(tile / uint(atlasColumns))) * brickSize;
        const vec2 atlasUv = (tileOrigin + local * brickSize) / (atlasColumns * brickSize);
        return texture(uSdfAtlas, atlasUv).r * sdf.flags.x;
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
    const float alphaCutoff = camera.materialParams.w > 0.5 && (vFlags & kRenderFlagDepthWrite) != 0u ? 0.5 : 0.002;
    if (color.a < alphaCutoff)
    {
        discard;
    }

    float shadow = 1.0;
    float ao = 1.0;
    if (sdf.flags.y > 0.5)
    {
        if (sdf.flags.z > 0.5)
        {
            shadow = texture(uShadow, vec3(vScreen, camera.frame.z)).r;
        }
        else
        {
            shadow = mix(sdf.sun.w, 1.0, visibility(vWorld));
        }
        ao = ambientOcclusion(vWorld);
    }
    if (camera.frame.w > 0.5)
    {
        color.rgb *= (0.15 + ClusterLighting(vScreen, camera.viewportAndZoom.xy)) * shadow * ao;
    }
    else if (sdf.flags.y > 0.5)
    {
        color.rgb *= shadow * ao;
    }
    if (giVolume.extent.w > 0.0)
    {
        float giOcclusion = 1.0;
        const vec3 gi = SampleGi(vWorld, giOcclusion);
        const float strength = clamp(giVolume.extent.w, 0.0, 1.0);
        color.rgb = mix(color.rgb, color.rgb * 0.5 + gi, strength * 0.5);
        color.rgb *= mix(1.0, giOcclusion, strength * 0.35);
    }
    outColor = color;
}
