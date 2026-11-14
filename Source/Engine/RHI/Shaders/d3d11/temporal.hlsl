Texture2D CurrentColor : register(t0);
Texture2D HistoryColor : register(t1);
Texture2D MotionVectors : register(t2);
Texture2D CurrentDepth : register(t3);
Texture2D HistoryDepth : register(t4);
SamplerState TemporalSampler : register(s0);
cbuffer TemporalParams : register(b0) { float HistoryWeight; float ResetHistory; float MotionScale; float DepthThreshold; };
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
    float4 current = CurrentColor.Sample(TemporalSampler, input.uv);
    float2 motion = MotionVectors.Sample(TemporalSampler, input.uv).rg * MotionScale;
    float4 history = HistoryColor.Sample(TemporalSampler, input.uv + motion);
    float currentDepth = CurrentDepth.Sample(TemporalSampler, input.uv).r;
    float historyDepth = HistoryDepth.Sample(TemporalSampler, input.uv + motion).r;
    float weight = ResetHistory > 0.5 ? 1.0 : HistoryWeight;
    if (abs(currentDepth - historyDepth) > DepthThreshold)
    {
        weight = 1.0;
    }
    return lerp(history, current, weight);
}
