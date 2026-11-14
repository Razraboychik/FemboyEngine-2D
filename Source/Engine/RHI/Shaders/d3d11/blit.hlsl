// FemboyEngine2D - fullscreen blit for presenting a render target.
Texture2D Source : register(t0);
SamplerState BlitSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 PSMain(VSOut input) : SV_Target
{
    return Source.Sample(BlitSampler, input.uv);
}
