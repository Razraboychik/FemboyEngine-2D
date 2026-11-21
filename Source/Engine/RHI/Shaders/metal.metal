// FemboyEngine 2.0 - Metal Shading Language library.
//
// This file is the canonical MSL source. The build embeds it into
// MetalShaderSource.hpp and MetalRHI compiles it with newLibraryWithSource: at
// startup, so a macOS build has no loose .metallib to ship.
//
// Layout contract with C++ (fby/rhi/Mesh.hpp):
//
//   MeshVertex      40 bytes, std430-compatible
//   SkinnedVertex   32 bytes, homogeneous float4 position
//   MeshSkinRange    8 bytes
//   MeshDrawData    80 bytes (float4x4 + four uints)
//   RenderItem      48 bytes
//   CameraData      24 bytes
//
// Metal's alignment rules for these structs match std430 for this field order,
// which is why the same C++ buffers feed both backends without a conversion.
// The LBS math is identical to mesh_skinning.comp: a full 4x4 palette, two
// influences, normalized weights, bind-pose fallback.

#include <metal_stdlib>
using namespace metal;

struct MeshVertex
{
    float2 position;
    float2 uv;
    uint color;
    uint padding; // range index
    uint2 bones;
    float2 weights;
};

struct SkinnedVertex
{
    float4 position; // homogeneous LBS result
    float2 uv;
    uint color;
    uint padding;
};

struct MeshSkinRange
{
    uint boneOffset;
    uint boneCount;
};

struct MeshDrawData
{
    float4x4 model;
    uint vertexBase;
    uint layer;
    uint flags; // bit 0: static geometry
    uint padding;
};

struct RenderItem
{
    float2 position;
    float2 size;
    float2 uvMin;
    float2 uvMax;
    uint layer;
    uint color;
    float rotation;
    uint flags;
};

struct CameraData
{
    float2 center;
    float2 viewport;
    float zoom;
    float padding;
};

struct MeshVertexOut
{
    float4 position [[position]];
    float2 uv;
    float4 color;
    uint layer [[flat]];
};

struct SpriteVertexOut
{
    float4 position [[position]];
    float2 uv;
    float4 color;
    uint layer [[flat]];
    uint flags [[flat]];
};

// ---------------------------------------------------------------------------
// Linear blend skinning. ONE dispatch covers every dynamic range in the frame:
// the vertex's padding field selects its MeshSkinRange, exactly like the Vulkan
// compute shader.
// ---------------------------------------------------------------------------
kernel void mesh_skinning(device const MeshVertex* bindVertices [[buffer(0)]],
                          device SkinnedVertex* skinnedVertices [[buffer(1)]],
                          constant float4x4* palette [[buffer(2)]],
                          constant MeshSkinRange* ranges [[buffer(3)]],
                          constant uint& vertexCount [[buffer(4)]],
                          uint index [[thread_position_in_grid]])
{
    if (index >= vertexCount)
    {
        return;
    }

    const MeshVertex vertex = bindVertices[index];
    const MeshSkinRange range = ranges[vertex.padding];

    float4 skinned = float4(0.0);
    float totalWeight = 0.0;
    for (uint influence = 0; influence < 2; ++influence)
    {
        const float weight = vertex.weights[influence];
        if (weight <= 0.0)
        {
            continue;
        }
        const uint bone = vertex.bones[influence];
        if (bone >= range.boneCount)
        {
            continue;
        }
        // Full 4x4. A bone can move a vertex in z and w; dropping that would
        // make the Metal and Vulkan backends diverge under a tilted camera.
        const float4 transformed = palette[range.boneOffset + bone] * float4(vertex.position, 0.0, 1.0);
        skinned += transformed * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0)
    {
        skinned = float4(vertex.position, 0.0, 1.0);
    }
    else
    {
        skinned /= totalWeight;
    }

    SkinnedVertex outputVertex;
    outputVertex.position = skinned;
    outputVertex.uv = vertex.uv;
    outputVertex.color = vertex.color;
    outputVertex.padding = 0;
    skinnedVertices[index] = outputVertex;
}

// Static and skinned geometry share SkinnedVertex. The draw flags select the
// source; both buffers are always bound.
vertex MeshVertexOut mesh_vertex(uint vertexId [[vertex_id]],
                                 uint instanceId [[instance_id]],
                                 constant SkinnedVertex* skinnedVertices [[buffer(0)]],
                                 constant SkinnedVertex* staticVertices [[buffer(1)]],
                                 constant MeshDrawData& draw [[buffer(2)]],
                                 constant float4x4& viewProjection [[buffer(3)]])
{
    (void)instanceId;
    const uint index = draw.vertexBase + vertexId;
    SkinnedVertex vertex;
    if ((draw.flags & 1u) != 0u)
    {
        vertex = staticVertices[index];
    }
    else
    {
        vertex = skinnedVertices[index];
    }

    MeshVertexOut out;
    out.position = viewProjection * (draw.model * vertex.position);
    out.uv = vertex.uv;
    out.color = unpack_unorm4x8(vertex.color);
    out.layer = draw.layer;
    return out;
}

fragment float4 mesh_fragment(MeshVertexOut in [[stage_in]],
                              texture2d_array<float> atlas [[texture(0)]],
                              sampler atlasSampler [[sampler(0)]])
{
    const float4 texel = atlas.sample(atlasSampler, in.uv, in.layer);
    float4 color = texel * in.color;
    const uint normalLayer = (in.flags >> 16u) & 0xFFu;
    const float normalStrength = float((in.flags >> 11u) & 0x1Fu) / 31.0f;
    if (normalLayer >= 1u && normalStrength > 0.0f)
    {
        float3 normal = atlas.sample(atlasSampler, in.uv, normalLayer).xyz * 2.0f - 1.0f;
        normal.xy *= normalStrength;
        normal = normalize(float3(normal.xy, max(normal.z, 0.05f)));
        float3 lit = float3(0.15f, 0.15f, 0.15f);
        for (uint index = 0u; index < 4u; ++index)
        {
            const float radius = lights.positionRadius[index].z;
            if (radius <= 0.0f)
            {
                continue;
            }
            const float2 delta = lights.positionRadius[index].xy - in.position.xy;
            const float distance = length(delta);
            if (distance >= radius)
            {
                continue;
            }
            const float2 direction = delta / max(distance, 0.0001f);
            const float ndl = saturate(dot(normal.xy, direction));
            const float falloff = 1.0f - distance / radius;
            lit += lights.color[index].rgb * lights.color[index].a * falloff * falloff * (0.25f + 0.75f * ndl);
        }
        color.rgb *= lit;
    }
    if (color.a < 0.002)
    {
        discard_fragment();
    }
    return color;
}

// ---------------------------------------------------------------------------
// Sprite path (the 48-byte RenderItem contract). Kept in the same library so a
// Metal build has one compilation unit and one pipeline cache key space.
// ---------------------------------------------------------------------------
vertex SpriteVertexOut sprite_vertex(uint vertexId [[vertex_id]],
                                     uint instanceId [[instance_id]],
                                     constant RenderItem* items [[buffer(0)]],
                                     constant CameraData& camera [[buffer(1)]])
{
    const RenderItem item = items[instanceId];

    // Two triangles, no index buffer: 0,1,2, 3,4,5.
    const float2 corners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                               float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};
    float2 corner = corners[vertexId];

    const bool flipX = (item.flags & (1u << 0)) != 0u;
    const bool flipY = (item.flags & (1u << 1)) != 0u;
    if (flipX)
    {
        corner.x = 1.0 - corner.x;
    }
    if (flipY)
    {
        corner.y = 1.0 - corner.y;
    }

    const float2 local = (corner - 0.5) * item.size;
    const float cosine = cos(item.rotation);
    const float sine = sin(item.rotation);
    const float2 rotated = float2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);
    const float2 world = item.position + rotated;

    const float2 view = (world - camera.center) * camera.zoom;
    const float2 clip = view / (camera.viewport * 0.5);

    SpriteVertexOut out;
    out.position = float4(clip.x, -clip.y, 0.0, 1.0);
    out.uv = mix(item.uvMin, item.uvMax, corner);
    out.color = unpack_unorm4x8(item.color);
    out.layer = item.layer;
    out.flags = item.flags;
    return out;
}

struct SpriteLights
{
    float4 positionRadius[4];
    float4 color[4];
};

fragment float4 sprite_base_fragment(SpriteVertexOut in [[stage_in]],
                                    texture2d_array<float> atlas [[texture(0)]],
                                    sampler atlasSampler [[sampler(0)]])
{
    const float4 color = atlas.sample(atlasSampler, in.uv, in.layer) * in.color;
    if (color.a < 0.002f)
    {
        discard_fragment();
    }
    return color;
}

fragment float4 sprite_normal_fragment(SpriteVertexOut in [[stage_in]],
                                       texture2d_array<float> atlas [[texture(0)]],
                                       sampler atlasSampler [[sampler(0)]],
                                       constant SpriteLights& lights [[buffer(2)]])
{
    float4 color = atlas.sample(atlasSampler, in.uv, in.layer) * in.color;
    const uint normalLayer = (in.flags >> 16u) & 0xFFu;
    const float normalStrength = float((in.flags >> 11u) & 0x1Fu) / 31.0f;
    if (normalLayer >= 1u && normalStrength > 0.0f)
    {
        float3 normal = atlas.sample(atlasSampler, in.uv, normalLayer).xyz * 2.0f - 1.0f;
        normal.xy *= normalStrength;
        normal = normalize(float3(normal.xy, max(normal.z, 0.05f)));
        float3 lit = float3(0.15f, 0.15f, 0.15f);
        for (uint index = 0u; index < 4u; ++index)
        {
            const float radius = lights.positionRadius[index].z;
            if (radius <= 0.0f)
            {
                continue;
            }
            const float2 delta = lights.positionRadius[index].xy - in.position.xy;
            const float distance = length(delta);
            if (distance >= radius)
            {
                continue;
            }
            const float2 direction = delta / max(distance, 0.0001f);
            const float ndl = saturate(dot(normal.xy, direction));
            const float falloff = 1.0f - distance / radius;
            lit += lights.color[index].rgb * lights.color[index].a * falloff * falloff * (0.25f + 0.75f * ndl);
        }
        color.rgb *= lit;
    }
    if (color.a < 0.002f)
    {
        discard_fragment();
    }
    return color;
}

fragment float4 sprite_fragment(SpriteVertexOut in [[stage_in]],
                                texture2d_array<float> atlas [[texture(0)]],
                                sampler atlasSampler [[sampler(0)]],
                                constant SpriteLights& lights [[buffer(2)]])
{
    if ((in.flags & (1u << 6)) != 0u)
    {
        const float distance = atlas.sample(atlasSampler, in.uv, in.layer).r;
        const float antialias = max(fwidth(distance), 0.0005f);
        const float effectWidth = float((in.flags >> 16u) & 0xFFu) / 255.0f;
        float alpha;
        if ((in.flags & (1u << 7)) != 0u)
        {
            const float outer = 0.5f + max(effectWidth, antialias);
            alpha = smoothstep(0.5f, 0.5f + antialias, distance) -
                    smoothstep(outer, outer + antialias, distance);
        }
        else if ((in.flags & (1u << 8)) != 0u)
        {
            alpha = 1.0f - smoothstep(0.5f, 0.5f + max(effectWidth, antialias), distance);
        }
        else
        {
            alpha = smoothstep(0.5f - antialias, 0.5f + antialias, distance);
        }
        return float4(in.color.rgb, in.color.a * alpha);
    }
    if ((in.flags & ((1u << 7) | (1u << 8))) != 0u)
    {
        const float effectWidth = float((in.flags >> 16u) & 0xFFu) / 255.0f;
        const float2 texelSize = max(fwidth(in.uv), float2(0.00001f));
        const float radius = 1.0f + effectWidth * 8.0f;
        float mask = atlas.sample(atlasSampler, in.uv, in.layer).a;
        for (uint index = 0u; index < 8u; ++index)
        {
            const float angle = float(index) * 0.7853981634f;
            const float2 offset = float2(cos(angle), sin(angle)) * texelSize * radius;
            mask = max(mask, atlas.sample(atlasSampler, in.uv + offset, in.layer).a);
        }
        return float4(in.color.rgb, in.color.a * mask);
    }
    const float4 texel = atlas.sample(atlasSampler, in.uv, in.layer);
    const float4 color = texel * in.color;
    if (color.a < 0.002)
    {
        discard_fragment();
    }
    return color;
}

// ---------------------------------------------------------------------------
// Spatial upscale: one fullscreen triangle with nearest, bilinear or bicubic
// filtering. FSR1 has no MSL port and uses the bicubic filter.
// ---------------------------------------------------------------------------
struct UpscaleVertexOut
{
    float4 position [[position]];
    float2 uv;
};

struct UpscaleParams
{
    float2 sourceSize;
    // 0 nearest, 1 bilinear, 2 bicubic.
    uint mode;
    float padding;
};

static float fby_cubic_weight(float value)
{
    const float a = abs(value);
    if (a < 1.0f)
    {
        return 1.0f - 2.0f * a * a + a * a * a;
    }
    if (a < 2.0f)
    {
        return 4.0f - 8.0f * a + 5.0f * a * a - a * a * a;
    }
    return 0.0f;
}

vertex UpscaleVertexOut upscale_vertex(uint vertexId [[vertex_id]])
{
    const float2 uv = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    UpscaleVertexOut out;
    out.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    out.uv = uv;
    return out;
}

fragment float4 upscale_fragment(UpscaleVertexOut in [[stage_in]],
                                 texture2d<float> source [[texture(0)]],
                                 sampler sourceSampler [[sampler(0)]],
                                 constant UpscaleParams& params [[buffer(0)]])
{
    if (params.mode == 0u)
    {
        const uint2 texel = uint2(in.uv * params.sourceSize);
        const uint2 clamped = min(texel, uint2(params.sourceSize) - 1u);
        return source.read(clamped);
    }
    if (params.mode == 1u)
    {
        return source.sample(sourceSampler, in.uv);
    }
    const float2 coord = in.uv * params.sourceSize;
    const float2 base = floor(coord - 0.5f);
    const uint2 limit = uint2(params.sourceSize) - 1u;
    float4 accumulated = float4(0.0f);
    float weightSum = 0.0f;
    for (uint y = 0u; y < 4u; ++y)
    {
        for (uint x = 0u; x < 4u; ++x)
        {
            const float2 sampleCoord = base + float2(float(x), float(y));
            const float2 offset = coord - (sampleCoord + 0.5f);
            const float weight = fby_cubic_weight(offset.x) * fby_cubic_weight(offset.y);
            const uint2 texel = min(uint2(clamp(sampleCoord, float2(0.0f), float2(limit))), limit);
            accumulated += source.read(texel) * weight;
            weightSum += weight;
        }
    }
    return weightSum > 0.0f ? accumulated / weightSum : source.sample(sourceSampler, in.uv);
}

// ---------------------------------------------------------------------------
// Temporal resolve: current/history colour with motion reprojection and a
// depth-based disocclusion reject, matching the HLSL and GLSL semantics.
// ---------------------------------------------------------------------------
struct TemporalParams
{
    float historyWeight;
    float resetHistory;
    float motionScale;
    float depthThreshold;
};

fragment float4 temporal_fragment(UpscaleVertexOut in [[stage_in]],
                                  texture2d<float> currentColor [[texture(0)]],
                                  texture2d<float> historyColor [[texture(1)]],
                                  texture2d<float> motionVectors [[texture(2)]],
                                  texture2d<float> currentDepth [[texture(3)]],
                                  texture2d<float> historyDepth [[texture(4)]],
                                  sampler temporalSampler [[sampler(0)]],
                                  constant TemporalParams& params [[buffer(0)]])
{
    const float4 current = currentColor.sample(temporalSampler, in.uv);
    const float2 motion = motionVectors.sample(temporalSampler, in.uv).rg * params.motionScale;
    const float4 history = historyColor.sample(temporalSampler, in.uv - motion);
    const float currentDepthValue = currentDepth.sample(temporalSampler, in.uv).r;
    const float historyDepthValue = historyDepth.sample(temporalSampler, in.uv - motion).r;
    const float confidence = 1.0f - saturate(length(motion) * 8.0f);
    float historyWeight = params.resetHistory > 0.5f ? 0.0f : params.historyWeight * confidence;
    if (abs(currentDepthValue - historyDepthValue) > params.depthThreshold)
    {
        historyWeight = 0.0f;
    }
    return mix(current, history, saturate(historyWeight));
}

// ---------------------------------------------------------------------------
// HUD composite: fullscreen blended overlay with nearest/linear upscaling.
// ---------------------------------------------------------------------------
struct HudParams
{
    // 1 = nearest, 0 = linear.
    float nearest;
    float2 size;
    float padding;
};

fragment float4 hud_composite_fragment(UpscaleVertexOut in [[stage_in]],
                                       texture2d<float> hud [[texture(0)]],
                                       sampler nearestSampler [[sampler(0)]],
                                       sampler linearSampler [[sampler(1)]],
                                       constant HudParams& params [[buffer(0)]])
{
    if (params.nearest > 0.5f)
    {
        return hud.sample(nearestSampler, in.uv);
    }
    return hud.sample(linearSampler, in.uv);
}

// ---------------------------------------------------------------------------
// Motion vectors: the CPU expands rotated quads into NDC positions with a
// per-vertex UV delta, so the pass only forwards the delta.
// ---------------------------------------------------------------------------
struct MotionVertex
{
    packed_float3 position;
    float2 delta;
};

struct MotionOut
{
    float4 position [[position]];
    float2 delta;
};

vertex MotionOut motion_vertex(uint vertexId [[vertex_id]], constant MotionVertex* vertices [[buffer(0)]])
{
    MotionOut out;
    out.position = float4(float3(vertices[vertexId].position), 1.0f);
    out.delta = vertices[vertexId].delta;
    return out;
}

fragment float4 motion_fragment(MotionOut in [[stage_in]])
{
    return float4(in.delta, 0.0f, 1.0f);
}
