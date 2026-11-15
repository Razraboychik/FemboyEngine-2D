// FemboyEngine2D - bloom prefilter for Direct3D.
//
// A 16x16 tile is reduced to one bloom texel per invocation. The threshold is a
// soft knee so a bright sprite edge does not pop between mip levels.
cbuffer PostFxBlock : register(b0)
{
    float4 flagsAndCrt;
    float4 vignetteAndBloom;
    float4 exposureAndChromatic;
    float4 grainAndFlash;
};

Texture2D SceneColor : register(t0);
RWTexture2D<float4> BloomTarget : register(u0);
SamplerState BloomSampler : register(s0);

[numthreads(16, 16, 1)] void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    BloomTarget.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height)
    {
        return;
    }
    const float2 uv = (float2(threadId.xy) + 0.5) / float2(width, height);
    const float3 color = SceneColor.SampleLevel(BloomSampler, uv, 0.0).rgb * vignetteAndBloom.w;
    const float threshold = vignetteAndBloom.y;
    const float knee = max(threshold * 0.35, 0.0001);
    const float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float soft = clamp((luminance - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    const float weight = max(luminance - threshold, 0.0) + soft * knee;
    BloomTarget[threadId.xy] = float4(color * (weight / max(luminance, 0.0001)), 1.0);
}
