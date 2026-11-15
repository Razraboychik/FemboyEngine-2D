// FemboyEngine2D - separable 9-tap bloom blur for Direct3D.
//
// One shader, two dispatches: grainAndFlash.y selects the horizontal direction.
cbuffer PostFxBlock : register(b0)
{
    float4 flagsAndCrt;
    float4 vignetteAndBloom;
    float4 exposureAndChromatic;
    float4 grainAndFlash;
};

Texture2D BloomIn : register(t0);
RWTexture2D<float4> BloomOut : register(u0);
SamplerState BloomSampler : register(s0);

[numthreads(16, 16, 1)] void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    BloomOut.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height)
    {
        return;
    }
    const float2 direction = grainAndFlash.y > 0.5 ? float2(1.0, 0.0) : float2(0.0, 1.0);
    const float2 texel = direction / float2(width, height);
    const float2 uv = (float2(threadId.xy) + 0.5) / float2(width, height);
    float3 color = BloomIn.SampleLevel(BloomSampler, uv, 0.0).rgb * 0.227027;
    color += BloomIn.SampleLevel(BloomSampler, uv + texel * 1.384615, 0.0).rgb * 0.316216;
    color += BloomIn.SampleLevel(BloomSampler, uv - texel * 1.384615, 0.0).rgb * 0.316216;
    color += BloomIn.SampleLevel(BloomSampler, uv + texel * 3.230769, 0.0).rgb * 0.070270;
    color += BloomIn.SampleLevel(BloomSampler, uv - texel * 3.230769, 0.0).rgb * 0.070270;
    BloomOut[threadId.xy] = float4(color, 1.0);
}
