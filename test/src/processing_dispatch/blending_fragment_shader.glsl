#version 450

precision highp float;

layout(location = 0) out vec4 outColor;

void main()
{
    // Fractional alpha (0.5) is load-bearing: with blend_enable:true, this
    // forces a real src*0.5 + dst*0.5 floating-point mix against the render
    // target's clear color, unlike an opaque (alpha=1.0) draw where blending
    // would be a mathematical no-op.
    outColor = vec4(1.0, 0.0, 0.0, 0.5);
}
