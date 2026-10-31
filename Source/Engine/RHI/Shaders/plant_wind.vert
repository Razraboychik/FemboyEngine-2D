#version 450
// FemboyEngine 2.0 - wind deformation for plant/foliage quads.
//
// The same WeatherGpuConstants block that drives rain and cloth is bound here.
// aStiffness is authored per vertex (0 at the root, 1 at the tip); the sway is a
// phase-shifted sine driven by the resolved wind, not a second noise lookup.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aStiffness;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vTint;

layout(set = 0, binding = 0) uniform WeatherBlock
{
    vec4 windAndTime;
    vec4 atmosphere;
    vec4 clouds;
    vec4 precipitation;
    vec4 fog;
    vec4 fieldTransform;
    vec4 fieldExtent;
    vec4 volume;
} weather;

layout(push_constant) uniform PlantPush
{
    vec4 viewport; // xy pixels, zw inverse
    vec4 tint;
    vec4 anchorAndPhase; // xy world anchor, z phase, w amplitude
} plant;

void main()
{
    const float time = weather.windAndTime.z;
    const vec2 wind = weather.windAndTime.xy;
    const float speed = length(wind);
    const vec2 direction = speed > 0.001 ? wind / speed : vec2(0.0, 1.0);
    const float wave = sin(time * (2.2 + speed * 0.03) + plant.anchorAndPhase.z + aPosition.y * 0.02);
    const float bend = wave * aStiffness * aStiffness * plant.anchorAndPhase.w * clamp(speed * 0.05, 0.0, 1.0);
    const vec2 world = plant.anchorAndPhase.xy + aPosition + direction * bend;
    vUv = aUv;
    vTint = plant.tint;
    gl_Position = vec4(world / plant.viewport.xy * 2.0 - 1.0, 0.0, 1.0);
}
