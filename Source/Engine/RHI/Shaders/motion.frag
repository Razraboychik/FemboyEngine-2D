#version 460
// FemboyEngine2D - write signed screen-space motion into an RG16F target.

layout(location = 0) in vec2 vMotion;
layout(location = 1) in vec2 vUv;
layout(location = 2) flat in float vReactive;
layout(location = 0) out vec2 outMotion;

void main()
{
    // vMotion is the normalized UV delta (current - previous). FSR2 scales it by
    // the render extent; the reactive mask is not sampled by this backend.
    outMotion = vMotion;
}
