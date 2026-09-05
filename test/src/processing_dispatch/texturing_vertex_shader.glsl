#version 450

precision highp float;

layout(location = 0) in float inPosX;
layout(location = 1) in float inPosY;
layout(location = 2) in float inUvX;
layout(location = 3) in float inUvY;

layout(location = 0) out vec2 outUV;

void main()
{
    gl_Position = vec4(inPosX, inPosY, 0.0, 1.0);
    outUV = vec2(inUvX, inUvY);
}
