#version 450
// FemboyEngine 2.0 - instanced quad.
//
// One instance = one RenderItem (48 bytes). The quad geometry is a static index
// buffer (0..5); the vertex shader builds the corners, so the engine uploads no
// vertex buffer per sprite and no per-draw constants beyond the camera.
//
// The push block also carries the virtual-texture mapping: for items marked with
// RenderFlags::VirtualTexture the fragment shader resolves the world position
// through the page table instead of sampling the atlas.

layout(location = 0) in vec2 inPosition; // world-space centre
layout(location = 1) in vec2 inSize;     // world-space extent
layout(location = 2) in vec2 inUvMin;
layout(location = 3) in vec2 inUvMax;
layout(location = 4) in uint inLayer;    // texture-array slice
layout(location = 5) in uint inColor;    // packed RGBA8
layout(location = 6) in float inRotation; // radians
layout(location = 7) in uint inFlags;

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
} camera;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uint vLayer;
// World-space position of this corner: the SDF shadow solve runs per pixel.
layout(location = 3) out vec2 vWorld;
// NDC+0.5: the sampling UV of the screen-space SDF shadow mask.
layout(location = 4) out vec2 vScreen;
layout(location = 5) flat out uint vFlags;

const uint kFlipX = 1u;
const uint kFlipY = 2u;

void main()
{
    // Two triangles, clockwise in screen space (y grows down).
    const vec2 kCorners[6] = vec2[6](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
        vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));

    const vec2 corner = kCorners[gl_VertexIndex];
    const vec2 local = corner * inSize;

    const float cosine = cos(inRotation);
    const float sine = sin(inRotation);
    const vec2 rotated = vec2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);

    const vec2 world = inPosition + rotated;
    const vec2 view = (world - camera.centerAndPadding.xy) * camera.viewportAndZoom.z;
    const vec2 clip = view / (camera.viewportAndZoom.xy * 0.5);

    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
    vWorld = world;
    vScreen = gl_Position.xy * 0.5 + 0.5;
    vFlags = inFlags;

    // uv from the corner: [-0.5, 0.5] -> [0, 1], with optional flips.
    vec2 t = corner + 0.5;
    if ((inFlags & kFlipX) != 0u)
    {
        t.x = 1.0 - t.x;
    }
    if ((inFlags & kFlipY) != 0u)
    {
        t.y = 1.0 - t.y;
    }

    vUv = mix(inUvMin, inUvMax, t);
    vLayer = inLayer;
    // unpackUnorm4x8 reads byte 0 as x; RenderItem stores RGBA in little-endian
    // order, which is exactly the R8G8B8A8 layout the texture array uses.
    vColor = unpackUnorm4x8(inColor);
}
