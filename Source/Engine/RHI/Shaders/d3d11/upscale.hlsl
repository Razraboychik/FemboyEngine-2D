// FemboyEngine2D - spatial upscale (nearest / bilinear / bicubic).
//
// FSR1 has no separate Direct3D port; it maps to the bicubic filter.
cbuffer UpscaleParams : register(b0)
{
    // x = mode (0 nearest, 1 bilinear, 2+ bicubic), yz = source size.
    float4 modeAndSize;
};

Texture2D Source : register(t0);
SamplerState UpscaleSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 Bilinear(float2 uv)
{
    return Source.Sample(UpscaleSampler, uv);
}

float4 Nearest(float2 uv, int2 size)
{
    const int2 pixel = clamp(int2(uv * float2(size)), int2(0, 0), size - 1);
    return Source.Load(int3(pixel, 0));
}

float4 Bicubic(float2 uv, int2 size)
{
    const float2 coord = uv * float2(size) - 0.5;
    const int2 base = int2(floor(coord));
    const float2 t = coord - float2(base);
    const float2 t2 = t * t;
    const float2 t3 = t2 * t;
    const float4 wx = float4(-0.5 * t3.x + t2.x - 0.5 * t.x, 1.5 * t3.x - 2.5 * t2.x + 1.0,
                            -1.5 * t3.x + 2.0 * t2.x + 0.5 * t.x, 0.5 * t3.x - 0.5 * t2.x);
    const float4 wy = float4(-0.5 * t3.y + t2.y - 0.5 * t.y, 1.5 * t3.y - 2.5 * t2.y + 1.0,
                            -1.5 * t3.y + 2.0 * t2.y + 0.5 * t.y, 0.5 * t3.y - 0.5 * t2.y);
    float4 sum = 0.0;
    [unroll] for (int y = 0; y < 4; ++y)
    {
        [unroll] for (int x = 0; x < 4; ++x)
        {
            const int2 pixel = clamp(base + int2(x - 1, y - 1), int2(0, 0), size - 1);
            sum += Source.Load(int3(pixel, 0)) * (wx[x] * wy[y]);
        }
    }
    return sum;
}

float4 PSMain(VSOut input) : SV_Target
{
    const int2 size = int2(modeAndSize.y, modeAndSize.z);
    if (modeAndSize.x < 0.5)
    {
        return Nearest(input.uv, size);
    }
    if (modeAndSize.x < 1.5)
    {
        return Bilinear(input.uv);
    }
    return Bicubic(input.uv, size);
}
