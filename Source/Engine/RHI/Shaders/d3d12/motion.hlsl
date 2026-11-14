struct VSInput { float3 position : POSITION; float2 delta : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float2 delta : TEXCOORD0; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.delta = input.delta;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target { return float4(input.delta * 0.5, 0.0, 1.0); }
