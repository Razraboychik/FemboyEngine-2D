Texture2DArray SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);
struct VSInput { float3 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; float4 materialVarying : TEXCOORD2; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.layer = input.layer;
    output.materialVarying = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    return SpriteTexture.Sample(SpriteSampler, float3(input.uv, input.layer)) * input.color;
}
