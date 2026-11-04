#version 460
// FemboyEngine2D - per-sprite screen-space motion vectors.

struct MotionData
{
    vec2 position;
    vec2 size;
    vec2 previousPosition;
    vec2 previousSize;
    float rotation;
    float previousRotation;
    vec2 screenDelta;
    uint flags;
};

layout(set = 0, binding = 0) readonly buffer MotionBuffer { MotionData items[]; };
layout(push_constant) uniform MotionConstants
{
    vec2 cameraCenter;
    vec2 previousCameraCenter;
    vec2 viewport;
    float cameraZoom;
    float previousCameraZoom;
    vec2 jitter;
    vec2 previousJitter;
} motion;
layout(location = 0) out vec2 vMotion;
layout(location = 1) out vec2 vUv;
layout(location = 2) flat out float vReactive;

void main()
{
    const MotionData ScopedItem = items[gl_InstanceIndex];
    const vec2 ScopedCorners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const vec2 ScopedLocal = ScopedCorners[gl_VertexIndex % 6];
    const vec2 ScopedCurrentLocal = ScopedLocal * ScopedItem.size * 0.5;
    const vec2 ScopedPreviousLocal = ScopedLocal * ScopedItem.previousSize * 0.5;
    const float ScopedCos = cos(ScopedItem.rotation);
    const float ScopedSin = sin(ScopedItem.rotation);
    const float ScopedPreviousCos = cos(ScopedItem.previousRotation);
    const float ScopedPreviousSin = sin(ScopedItem.previousRotation);
    const vec2 ScopedWorld = ScopedItem.position +
        vec2(ScopedCurrentLocal.x * ScopedCos - ScopedCurrentLocal.y * ScopedSin,
             ScopedCurrentLocal.x * ScopedSin + ScopedCurrentLocal.y * ScopedCos);
    const vec2 ScopedPreviousWorld = ScopedItem.previousPosition +
        vec2(ScopedPreviousLocal.x * ScopedPreviousCos - ScopedPreviousLocal.y * ScopedPreviousSin,
             ScopedPreviousLocal.x * ScopedPreviousSin + ScopedPreviousLocal.y * ScopedPreviousCos);
    const vec2 ScopedCurrentScreen =
        (ScopedWorld - motion.cameraCenter) * motion.cameraZoom + motion.jitter;
    const vec2 ScopedPreviousScreen =
        (ScopedPreviousWorld - motion.previousCameraCenter) * motion.previousCameraZoom + motion.previousJitter;
    const vec2 ScopedClip = ScopedCurrentScreen / (motion.viewport * 0.5);
    gl_Position = vec4(ScopedClip.x, -ScopedClip.y, 0.0, 1.0);
    vMotion = (ScopedCurrentScreen - ScopedPreviousScreen) / max(motion.viewport, vec2(1.0));
    vUv = ScopedLocal * 0.5 + 0.5;
    const uint ScopedReactiveBits = (1u << 2) | (1u << 3) | (1u << 10);
    vReactive = (ScopedItem.flags & ScopedReactiveBits) != 0u ? 1.0 : 0.0;
}
