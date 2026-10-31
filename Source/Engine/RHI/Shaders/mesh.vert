#version 450
// FemboyEngine 2.0 - mesh vertex shader for the indirect 2.5D pipeline.
//
// No vertex input binding and no per-mesh push constant. The indirect command's
// firstInstance selects one MeshDrawData record; every mesh in a multi-draw can
// therefore carry its own full 4x4 model matrix.
//
// Static geometry and skinned geometry use the same SkinnedVertex layout in two
// different SSBOs. A single flag in the draw record selects the source.

struct SkinnedVertex
{
    vec4 position;
    vec2 uv;
    uint color;
    uint padding;
};

struct MeshVertex
{
    vec2 position;
    vec2 uv;
    uint color;
    uint padding;
    uvec2 bones;
    vec2 weights;
};

struct MeshDrawData
{
    mat4 model;
    uint vertexBase;
    uint layer;
    uint flags;
    uint padding;
};

layout(set = 1, binding = 0) readonly buffer SkinnedVertices
{
    SkinnedVertex vertices[];
} skinned;

layout(set = 1, binding = 1) readonly buffer StaticVertices
{
    SkinnedVertex vertices[];
} staticVertices;

layout(set = 1, binding = 2) readonly buffer DrawData
{
    MeshDrawData draws[];
} drawData;

// Cloth and other CPU-deformed meshes. The RHI copies their bind-pose vertices
// here and sets MeshDrawUnskinned; no skinning compute dispatch is recorded.
layout(set = 1, binding = 3) readonly buffer UnskinnedVertices
{
    MeshVertex vertices[];
} unskinned;

layout(push_constant) uniform Push
{
    // Full 2.5D view-projection. Built from CameraData when the caller did not
    // supply a tilted/perspective matrix.
    mat4 viewProjection;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uint vLayer;

void main()
{
    // Vulkan defines gl_InstanceIndex as firstInstance + instance. The RHI sets
    // firstInstance to the draw record index and instanceCount to 1.
    const uint drawIndex = uint(gl_InstanceIndex);
    const MeshDrawData draw = drawData.draws[drawIndex];
    const uint vertexIndex = draw.vertexBase + uint(gl_VertexIndex);

    SkinnedVertex vertex;
    if ((draw.flags & 1u) != 0u)
    {
        vertex = staticVertices.vertices[vertexIndex];
    }
    else if ((draw.flags & 2u) != 0u)
    {
        // Unskinned path: the bind-pose position is already final. No palette
        // index exists for this vertex, which is exactly why it must not run
        // through the skinning dispatch.
        const MeshVertex source = unskinned.vertices[vertexIndex];
        vertex.position = vec4(source.position, 0.0, 1.0);
        vertex.uv = source.uv;
        vertex.color = source.color;
        vertex.padding = 0;
    }
    else
    {
        vertex = skinned.vertices[vertexIndex];
    }

    // Full 4x4 model, then full 4x4 view-projection. The homogeneous w is
    // preserved for perspective tilts.
    const vec4 world = draw.model * vertex.position;
    gl_Position = pc.viewProjection * world;

    vUv = vertex.uv;
    vColor = unpackUnorm4x8(vertex.color);
    vLayer = draw.layer;
}
