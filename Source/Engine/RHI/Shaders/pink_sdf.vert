#version 450
// FemboyEngine 2.0 - procedural Pink UI vertex stage.
//
// One instance = one primitive. There is no vertex buffer and no texture array
// layer: the quad is generated from gl_VertexIndex and the record is fetched from
// a std430 buffer. A batch of 10k buttons is one instanced draw.

struct PinkSdfPrimitive
{
    vec4 rect;   // xy centre, zw size
    vec4 params; // x radius, y border, z rotation, w shadow
    vec4 fill;   // rgba
    vec4 border; // rgba, w = flags | shape << 8
};

layout(std430, set = 0, binding = 0) readonly buffer Primitives
{
    PinkSdfPrimitive primitives[];
} batch;

layout(set = 0, binding = 1) uniform ViewportBlock
{
    vec4 viewport; // xy pixels, zw inverse
} view;

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec4 vFill;
layout(location = 2) out vec4 vBorder;
layout(location = 3) out vec4 vParams;
layout(location = 4) out vec4 vShape;

void main()
{
    const PinkSdfPrimitive primitive = batch.primitives[gl_InstanceIndex];
    const vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    const vec2 local = corner * primitive.rect.zw * 0.5;
    const float angle = primitive.params.z;
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const vec2 rotated = vec2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);
    vLocal = local;
    vFill = primitive.fill;
    vBorder = primitive.border;
    vParams = primitive.params;
    vShape = primitive.rect;
    // The UI pass uploads already projected pixel coordinates. A game with a
    // 2.5D HUD can still use the same primitive buffer by pre-transforming
    // rect.xy on the CPU.
    gl_Position = vec4((primitive.rect.xy + rotated) / view.viewport.xy * 2.0 - 1.0, 0.0, 1.0);
}
