// FemboyEngine2D - composite a premultiplied HUD target over the scene.
cbuffer HudBlock : register(b0)
{
    // x = 1 for nearest sampling, y/z = HUD target size, w reserved.
    float4 modeAndSize;
};

Texture2D HudTexture : register(t0);
SamplerState HudSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 PSMain(VSOut input) : SV_Target
{
    float4 hud;
    if (modeAndSize.x > 0.5)
    {
        const int2 size = int2(modeAndSize.y, modeAndSize.z);
        const int2 pixel = clamp(int2(input.uv * float2(size)), int2(0, 0), size - 1);
        hud = HudTexture.Load(int3(pixel, 0));
    }
    else
    {
        hud = HudTexture.Sample(HudSampler, input.uv);
    }
    if (hud.a <= 0.0001)
    {
        discard;
    }
    // The HUD target is premultiplied; the blend state is straight alpha.
    return float4(hud.rgb / hud.a, hud.a);
}
