Texture2D SourceColor : register(t0);
SamplerState SharpenSampler : register(s0);
cbuffer SharpenParams : register(b0) { float Sharpness; float TexelX; float TexelY; float Padding; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    float4 center = SourceColor.Sample(SharpenSampler, input.uv);
    float4 left = SourceColor.Sample(SharpenSampler, input.uv + float2(-TexelX, 0.0));
    float4 right = SourceColor.Sample(SharpenSampler, input.uv + float2(TexelX, 0.0));
    float4 up = SourceColor.Sample(SharpenSampler, input.uv + float2(0.0, -TexelY));
    float4 down = SourceColor.Sample(SharpenSampler, input.uv + float2(0.0, TexelY));
    float4 sharpened = center + (center * 4.0 - left - right - up - down) * Sharpness;
    return float4(sharpened.rgb, center.a);
}
