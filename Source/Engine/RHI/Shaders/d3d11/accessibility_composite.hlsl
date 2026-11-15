// FemboyEngine2D - accessibility composite for Direct3D.
//
// Applies the colour-vision correction matrix and the per-pixel flash guard,
// mirroring accessibility_composite.frag.
cbuffer AccessibilityBlock : register(b0)
{
    float4 matrix0;
    float4 matrix1;
    float4 matrix2;
    float4 safety;
    float4 flashGuard;
};

Texture2D SceneColor : register(t0);
Texture2D<float> FlashHistory : register(t1);
SamplerState AccessibilitySampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 PSMain(VSOut input) : SV_Target
{
    float4 color = SceneColor.Sample(AccessibilitySampler, input.uv);
    if (flashGuard.z > 0.5)
    {
        uint width = 0;
        uint height = 0;
        FlashHistory.GetDimensions(width, height);
        const int2 pixel = clamp(int2(input.uv * float2(width, height)), int2(0, 0), int2(width - 1, height - 1));
        const float raw = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        const float previous = FlashHistory.Load(int3(pixel, 0));
        if (previous > 0.0 && abs(raw - previous) > flashGuard.x)
        {
            color.rgb *= saturate(flashGuard.y);
        }
    }
    const uint flags = (uint)(safety.z + 0.5);
    if ((flags & 1u) != 0u)
    {
        const float3x3 correction = float3x3(matrix0.xyz, matrix1.xyz, matrix2.xyz);
        color.rgb = lerp(color.rgb, mul(color.rgb, correction), saturate(safety.y));
    }
    if ((flags & 2u) != 0u)
    {
        color.rgb *= saturate(safety.x);
    }
    return float4(color.rgb, 1.0);
}
