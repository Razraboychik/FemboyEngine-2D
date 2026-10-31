#version 450
// FemboyEngine 2.0 - fullscreen triangle used by post and weather composites.

layout(location = 0) out vec2 vUv;

void main()
{
    const vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
