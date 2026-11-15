// FemboyEngine2D - writes the per-pixel luminance history for the flash guard.
Texture2D<float4> SceneColor : register(t0);
RWTexture2D<float> FlashOut : register(u0);
SamplerState FlashSampler : register(s0);

[numthreads(8, 8, 1)] void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    FlashOut.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height)
    {
        return;
    }
    const float2 uv = (float2(threadId.xy) + 0.5) / float2(width, height);
    const float3 color = SceneColor.SampleLevel(FlashSampler, uv, 0.0).rgb;
    FlashOut[threadId.xy] = dot(color, float3(0.2126, 0.7152, 0.0722));
}
