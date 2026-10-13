#version 450

// Pass-through fragment shader for RenderProcessor conformance tests.
// Outputs solid white [1.0, 1.0, 1.0, 1.0] at location 0.
// Every pixel is deterministic: RGBA8 (255, 255, 255, 255).

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
