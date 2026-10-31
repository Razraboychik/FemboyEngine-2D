// FemboyEngine 2.0 - Metal rain/snow kernel.
//
// Mirrors weather_rain.comp. The C++ WeatherPreconciliatedSystem records the same
// WeatherGpuConstants block and the same rain/splash buffers; only the dispatch
// wrapper differs between Metal and Vulkan.
#include <metal_stdlib>
using namespace metal;

struct WeatherGpuConstants
{
    float4 windAndTime;
    float4 atmosphere;
    float4 clouds;
    float4 precipitation;
    float4 fog;
    float4 fieldTransform;
    float4 fieldExtent;
    float4 volume;
};

struct Particle
{
    float4 state0; // xy position, z age, w life
    float4 state1; // xy velocity, z type, w size
    float4 color;
    float4 seed;
};

struct Splash
{
    float4 state0;
    float4 color;
};

uint pcgNext(thread uint& state)
{
    state = state * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(thread uint& state)
{
    return float(pcgNext(state)) * (1.0 / 4294967296.0);
}

void spawnParticle(thread Particle& particle, thread uint& randomState, constant WeatherGpuConstants& weather)
{
    const bool snow = weather.precipitation.y > weather.precipitation.x;
    const float fallSpeed = snow ? weather.volume.w : weather.volume.z;
    const float2 area = max(weather.volume.xy, float2(1.0));
    const float2 origin = weather.fieldTransform.xy - area * 0.5;
    const float2 position = origin + float2(randomFloat(randomState) * area.x,
                                            area.y * (0.75 + randomFloat(randomState) * 0.25));
    const float life = snow ? mix(6.0, 14.0, randomFloat(randomState)) : mix(0.6, 1.4, randomFloat(randomState));
    const float size = snow ? mix(1.2, 3.0, randomFloat(randomState)) : mix(0.7, 1.8, randomFloat(randomState));
    const float2 wind = weather.windAndTime.xy * (snow ? 0.85 : 0.35);
    particle.state0 = float4(position, 0.0, life);
    particle.state1 = float4(wind.x, -fallSpeed + wind.y, snow ? 1.0 : 0.0, size);
    particle.color = snow ? float4(0.92, 0.94, 1.0, 0.9) : float4(0.63, 0.78, 1.0, 0.55);
    particle.seed = float4(float(randomState & 0xFFFFu), 0.0, 0.0, 0.0);
}

kernel void weatherRain(device Particle* particles [[buffer(0)]],
                        device Splash* splashes [[buffer(1)]],
                        device atomic_uint* splashCount [[buffer(2)]],
                        texture2d<float, access::sample> sdf [[texture(3)]],
                        constant WeatherGpuConstants& weather [[buffer(4)]],
                        uint particleIndex [[thread_position_in_grid]])
{
    const uint count = uint(weather.fieldExtent.z);
    if (particleIndex >= count)
    {
        return;
    }
    constexpr sampler linearSampler(coord::normalized, filter::linear, address::clamp_to_edge);
    Particle particle = particles[particleIndex];
    uint seedState = uint(weather.windAndTime.z * 1000.0);
    seedState ^= particleIndex * 747796405u;
    seedState ^= uint(weather.clouds.x * 1000.0);
    uint randomState = pcgNext(seedState);
    if (particle.state0.w <= 0.0)
    {
        spawnParticle(particle, randomState, weather);
    }
    const float dt = max(weather.windAndTime.w, 0.0);
    const bool snow = particle.state1.z > 0.5;
    const float drag = snow ? 1.8 : 0.6;
    const float2 wind = weather.windAndTime.xy * (snow ? 0.9 : 0.45);
    float2 velocity = particle.state1.xy;
    velocity += (wind - velocity) * min(dt * drag, 1.0);
    particle.state0.z += dt;
    const float2 previous = particle.state0.xy;
    particle.state0.xy += velocity * dt;

    const float2 fieldSize = max(weather.fieldExtent.xy * weather.fieldTransform.zw, float2(0.001));
    const float2 uv = (particle.state0.xy - weather.fieldTransform.xy) / fieldSize;
    float surface = 1.0e6;
    if (all(uv >= 0.0) && all(uv <= 1.0))
    {
        surface = sdf.sample(linearSampler, uv).r;
    }
    const float2 previousUv = (previous - weather.fieldTransform.xy) / fieldSize;
    const float previousSurface =
        (all(previousUv >= 0.0) && all(previousUv <= 1.0)) ? sdf.sample(linearSampler, previousUv).r : 1.0e6;
    if (surface <= 0.0 && previousSurface > 0.0)
    {
        const uint splashIndex = atomic_fetch_add_explicit(splashCount, 1u, memory_order_relaxed);
        if (splashIndex < uint(weather.fieldExtent.w))
        {
            const float energy = clamp((previousSurface - surface) / max(particle.state1.w, 0.1), 0.0, 1.0);
            splashes[splashIndex].state0 = float4(previous, 0.0, energy);
            splashes[splashIndex].color = float4(0.75, 0.88, 1.0, 0.65 * energy);
        }
        spawnParticle(particle, randomState, weather);
    }
    else if (particle.state0.z >= particle.state0.w)
    {
        spawnParticle(particle, randomState, weather);
    }
    particles[particleIndex] = particle;
}
