#version 450
// FemboyEngine 2.0 - splash quad vertex stage.
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

struct Splash
{
    vec4 state0;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer Splashes
{
    uint splashCount;
    uint padding0;
    uint padding1;
    uint padding2;
    Splash splashes[];
} splashBuffer;

layout(set = 0, binding = 1) uniform WeatherBlock
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
    const Splash splash = splashBuffer.splashes[gl_InstanceIndex];
    if (splash.state0.w <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    const vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    const float size = 10.0 + splash.state0.w * 18.0;
    const vec2 area = max(weather.volume.xy, vec2(1.0));
    const vec2 world = splash.state0.xy + corner * size;
    const vec2 clip = (world - (weather.fieldTransform.xy - area * 0.5)) / area * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
    vUv = corner * 0.5 + 0.5;
    vColor = splash.color;
}
