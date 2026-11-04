#version 460
// FemboyEngine2D - composite a premultiplied HUD target over the scene.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uHud;
layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 ScopedHud = texture(uHud, vUv);
    if (ScopedHud.a <= 0.0001)
    {
        discard;
    }
    // The HUD target is composited with straight alpha by the pipeline.
    outColor = vec4(ScopedHud.rgb / ScopedHud.a, ScopedHud.a);
}
