#version 450

precision highp float;

layout(location = 0) in vec2 inUV;
layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(texSampler, inUV);
}
