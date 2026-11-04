#version 450
// FemboyEngine2D - depth-only prepass fragment shader.
//
// The prepass writes gl_Position.z from quad.vert and has no color attachment.
// An empty fragment stage keeps the pipeline legal without a color target.

void main()
{
}
