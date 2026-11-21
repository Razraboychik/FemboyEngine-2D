Texture2DArray SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);
struct GpuLight
{
    float4 PositionAndRadius;
    float4 ColorAndIntensity;
    float4 DirectionAndCone;
    uint Type;
    uint Flags;
    uint ShadowIndex;
    uint Reserved;
};
StructuredBuffer<GpuLight> SpriteLights : register(t1);
struct VSInput { float3 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; float normalMeta : TEXCOORD2; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR; float layer : TEXCOORD1; float normalMeta : TEXCOORD3; };
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.layer = input.layer;
    output.normalMeta = input.normalMeta;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target
{
    float4 base = SpriteTexture.Sample(SpriteSampler, float3(input.uv, input.layer)) * input.color;
    float layer = floor(input.normalMeta);
    float strength = frac(input.normalMeta) * 2.0;
    if (layer >= 1.0 && strength > 0.0)
    {
        float3 sampled = SpriteTexture.Sample(SpriteSampler, float3(input.uv, layer)).xyz * 2.0 - 1.0;
        sampled.xy *= strength;
        float3 normal = normalize(float3(sampled.xy, max(sampled.z, 0.05)));
        float3 lit = float3(0.15, 0.15, 0.15);
        const uint lightCount = min((uint)SpriteLights[63].PositionAndRadius.w, 63u);
        [loop] for (uint i = 0; i < lightCount; ++i)
        {
            GpuLight light = SpriteLights[i];
            float radius = light.PositionAndRadius.z;
            if (radius <= 0.0)
            {
                continue;
            }
            float2 delta = light.PositionAndRadius.xy - input.position.xy;
            float distance = length(delta);
            if (distance >= radius)
            {
                continue;
            }
            float2 direction = delta / max(distance, 0.0001);
            float ndl = saturate(dot(normal.xy, direction));
            float falloff = 1.0 - distance / radius;
            lit += light.ColorAndIntensity.rgb * light.ColorAndIntensity.a * falloff * falloff * (0.25 + 0.75 * ndl);
        }
        base.rgb *= lit;
    }
    return base;
}
