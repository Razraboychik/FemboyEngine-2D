#version 450
// FemboyEngine 2.0 - Pink UI SDF circle / rounded box / capsule / card / ring.
//
// No texture fetch is issued in this shader. Anti-aliasing uses the analytic
// derivative of the distance with fwidth, so the edge stays one pixel at any
// resolution or UI scale.

layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec4 vFill;
layout(location = 2) in vec4 vBorder;
layout(location = 3) in vec4 vParams;
layout(location = 4) in vec4 vShape;
layout(location = 0) out vec4 outColor;

float sdCircle(vec2 p, float r)
{
    return length(p) - r;
}

float sdRoundedBox(vec2 p, vec2 halfSize, float r)
{
    const vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float sdCapsule(vec2 p, float halfLength, float radius)
{
    vec2 q = p;
    q.x = abs(q.x) - halfLength;
    q.x = max(q.x, 0.0);
    return length(q) - radius;
}

float sdRing(vec2 p, float radius, float thickness)
{
    return abs(length(p) - radius) - thickness;
}

void main()
{
    const uint flags = uint(vBorder.w) & 0xFFu;
    const uint shape = (uint(vBorder.w) >> 8) & 0x0Fu;
    const vec2 p = vLocal;
    const vec2 halfSize = vShape.zw * 0.5;
    float distance;
    if (shape == 0u)
    {
        distance = sdCircle(p, max(vParams.x, 0.0));
    }
    else if (shape == 2u)
    {
        distance = sdCapsule(p, max(halfSize.x - halfSize.y, 0.0), max(halfSize.y, 0.0));
    }
    else if (shape == 4u)
    {
        distance = sdRing(p, max(vParams.x, 0.0), max(vParams.y, 0.0));
    }
    else
    {
        distance = sdRoundedBox(p, halfSize, clamp(vParams.x, 0.0, min(halfSize.x, halfSize.y)));
    }
    const float alphaWidth = max(fwidth(distance), 0.0005);
    const float fillMask = 1.0 - smoothstep(-alphaWidth, alphaWidth, distance);
    if (fillMask <= 0.001)
    {
        discard;
    }
    vec4 color = vFill;
    if ((shape == 3u || shape == 1u) && vParams.y > 0.0)
    {
        const float borderDistance = abs(distance + vParams.y) - vParams.y;
        const float borderMask = 1.0 - smoothstep(-alphaWidth, alphaWidth, borderDistance);
        color = mix(color, vBorder, borderMask);
    }
    if ((flags & 8u) != 0u)
    {
        // Selected cards get an inner pink glow rather than a second draw.
        color.rgb += vec3(0.18, 0.03, 0.10) * (1.0 - smoothstep(0.0, vParams.y + 3.0, abs(distance)));
    }
    color.a *= fillMask;
    outColor = color;
}
