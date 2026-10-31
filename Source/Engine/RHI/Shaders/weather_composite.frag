#version 450
// FemboyEngine 2.0 - weather composite.
//
// Samples the four transient weather targets and blends them over the scene in a
// fixed order: cloud shadow, fog, cloud colour, fire/smoke. Nothing is uploaded
// per texel; the only CPU-side weather data is the shared uniform block.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uClouds;
layout(set = 0, binding = 2) uniform sampler2D uCloudShadow;
layout(set = 0, binding = 3) uniform sampler2D uFog;
layout(set = 0, binding = 4) uniform sampler2D uFire;

layout(set = 0, binding = 5) uniform WeatherBlock
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
    vec4 scene = texture(uScene, vUv);
    const float shadow = texture(uCloudShadow, vUv).r;
    scene.rgb *= mix(1.0, shadow, weather.clouds.w);
    const vec4 fogSample = texture(uFog, vUv);
    scene.rgb = mix(scene.rgb, fogSample.rgb, clamp(fogSample.a * weather.fog.x * 26.0, 0.0, 0.85));
    const vec4 cloud = texture(uClouds, vUv);
    scene.rgb = mix(scene.rgb, cloud.rgb, clamp(cloud.a * 0.72, 0.0, 0.85));
    const vec4 fire = texture(uFire, vUv);
    scene.rgb = mix(scene.rgb, fire.rgb, clamp(fire.a, 0.0, 0.9));
    outColor = scene;
}
