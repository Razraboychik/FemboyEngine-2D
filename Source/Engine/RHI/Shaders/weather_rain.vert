#version 450
// FemboyEngine 2.0 - rain drops as procedural line segments.
//
// Two vertices per particle, no vertex buffer: the particle SSBO is the geometry
// source. The line length is derived from the velocity, so a gust stretches every
// drop in the same direction as the cloth and foliage.

layout(location = 0) out vec4 vColor;

struct Particle
{
    vec4 state0;
    vec4 state1;
    vec4 color;
    vec4 seed;
};

layout(std430, set = 0, binding = 0) readonly buffer Particles
{
    Particle particles[];
} particleBuffer;

struct Splash
{
    vec4 state0;
    vec4 color;
};

layout(std430, set = 0, binding = 1) readonly buffer Splashes
{
    uint splashCount;
    uint padding0;
    uint padding1;
    uint padding2;
    Splash splashes[];
} splashBuffer;

layout(set = 0, binding = 2) uniform WeatherBlock
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

void main()
{
    const Particle particle = particleBuffer.particles[gl_InstanceIndex];
    if (particle.state0.w <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    const vec2 area = max(weather.volume.xy, vec2(1.0));
    const vec2 world = particle.state0.xy;
    const vec2 velocity = particle.state1.xy;
    const float speed = length(velocity);
    const vec2 direction = speed > 0.001 ? velocity / speed : vec2(0.0, -1.0);
    const float lengthPixels = clamp(speed * 0.035, 3.0, 34.0) * particle.state1.w;
    vec2 position = world;
    if ((gl_VertexIndex & 1) != 0)
    {
        position -= direction * lengthPixels;
    }
    position.x += (float(gl_VertexIndex & 1) - 0.5) * particle.state1.w * 0.7;
    const vec2 clip = (position - (weather.fieldTransform.xy - area * 0.5)) / area * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
    vColor = vec4(particle.color.rgb, particle.color.a * (0.35 + 0.65 * float((gl_VertexIndex & 1) == 0)));
}
