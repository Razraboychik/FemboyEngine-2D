struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float layer : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    uint layer : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.layer = (uint)input.layer;
    return output;
}
