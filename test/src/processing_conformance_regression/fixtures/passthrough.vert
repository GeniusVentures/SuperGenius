#version 450

// Pass-through vertex shader for RenderProcessor conformance tests.
// Takes vec3 positions, passes through to gl_Position.
// No uniforms, no textures — trivially deterministic.

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = vec4(inPosition, 1.0);
}
