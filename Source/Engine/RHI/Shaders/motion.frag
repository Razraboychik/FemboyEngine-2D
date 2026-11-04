#version 460
// FemboyEngine2D - encode screen-space motion in the red/green channels.

layout(location = 0) in vec2 vMotion;
layout(location = 1) in vec2 vUv;
layout(location = 2) flat in float vReactive;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vMotion.x * 0.5 + 0.5, vMotion.y * 0.5 + 0.5, vReactive, 1.0);
}
