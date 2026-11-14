// FemboyEngine2D - LUT colour grading for Direct3D.
//
// The LUT is a 2D strip of N x N*N texels in layer 0 of the bound texture array.
cbuffer ColorGradingBlock : register(b0)
{
    float4 params0; // lut size, intensity, saturation, contrast
    float4 params1; // brightness, lut id low, lut id high, flags
};

Texture2D SceneColor : register(t0);
Texture2DArray LutTexture : register(t1);
SamplerState GradingSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float3 SampleLut(float3 color)
{
    const float size = max(params0.x, 2.0);
    const float maxIndex = size - 1.0;
    const float3 clamped = clamp(color, 0.0, 1.0);
    const float slice = floor(clamped.b * maxIndex + 0.5);
    const float2 uv = float2((clamped.r * maxIndex + 0.5) / size, (slice * size + clamped.g * maxIndex + 0.5) / (size * size));
    return LutTexture.Sample(GradingSampler, float3(uv, 0.0)).rgb;
}

float4 PSMain(VSOut input) : SV_Target
{
    float4 scene = SceneColor.Sample(GradingSampler, input.uv);
    float3 graded = SampleLut(scene.rgb);
    const float luminance = dot(graded, float3(0.2126, 0.7152, 0.0722));
    graded = lerp(float3(luminance, luminance, luminance), graded, params0.z);
    graded = (graded - 0.5) * params0.w + 0.5 + params1.x;
    scene.rgb = lerp(scene.rgb, graded, clamp(params0.y, 0.0, 1.0));
    return scene;
}
