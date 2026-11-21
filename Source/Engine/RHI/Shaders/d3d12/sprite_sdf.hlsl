Texture2DArray SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float layer : TEXCOORD1;
    float normalMeta : TEXCOORD2;
    uint flags : TEXCOORD4;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float layer : TEXCOORD1;
    uint flags : TEXCOORD4;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.layer = input.layer;
    output.flags = input.flags;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const uint sdfText = 1u << 6;
    const uint sdfOutline = 1u << 7;
    const uint sdfGlow = 1u << 8;
    const float distance = SpriteTexture.Sample(SpriteSampler, float3(input.uv, input.layer)).r;
    const float antialias = max(fwidth(distance), 0.0005);
    const float effectWidth = float((input.flags >> 16u) & 0xFFu) / 255.0;
    float alpha;
    if ((input.flags & sdfText) != 0u)
    {
        if ((input.flags & sdfOutline) != 0u)
        {
            const float outer = 0.5 + max(effectWidth, antialias);
            alpha = smoothstep(0.5, 0.5 + antialias, distance) -
                    smoothstep(outer, outer + antialias, distance);
        }
        else if ((input.flags & sdfGlow) != 0u)
        {
            alpha = 1.0 - smoothstep(0.5, 0.5 + max(effectWidth, antialias), distance);
        }
        else
        {
            alpha = smoothstep(0.5 - antialias, 0.5 + antialias, distance);
        }
        return float4(input.color.rgb, input.color.a * alpha);
    }
    return SpriteTexture.Sample(SpriteSampler, float3(input.uv, input.layer)) * input.color;
}
