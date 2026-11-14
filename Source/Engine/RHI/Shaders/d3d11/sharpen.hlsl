Texture2D SourceColor : register(t0);
SamplerState SharpenSampler : register(s0);
cbuffer SharpenParams : register(b0) { float Sharpness; float TexelX; float TexelY; float Padding; };
float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 center = SourceColor.Sample(SharpenSampler, uv);
    float4 left = SourceColor.Sample(SharpenSampler, uv + float2(-TexelX, 0.0));
    float4 right = SourceColor.Sample(SharpenSampler, uv + float2(TexelX, 0.0));
    float4 up = SourceColor.Sample(SharpenSampler, uv + float2(0.0, -TexelY));
    float4 down = SourceColor.Sample(SharpenSampler, uv + float2(0.0, TexelY));
    float4 sharpened = center + (center * 4.0 - left - right - up - down) * Sharpness;
    return float4(sharpened.rgb, center.a);
}
